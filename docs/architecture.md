# Arquitetura

## Visão geral

O projeto separa a coleta de dados do jogo da apresentação visual. A coleta
ocorre no loop de memória criado em `Main.cpp`; a apresentação ocorre no loop
de renderização fornecido por `OS-ImGui`.

## Threads e compartilhamento de estado

### Thread de Memory (`Main.cpp`)

`Run()` inicializa as configurações e as features e inicia `MemoryLoop()` em uma
thread separada. Em cada iteração, essa thread:

1. atualiza o cache do `Engine`;
2. atualiza atores, granadas e câmera;
3. copia o snapshot para `GSharedData` sob proteção de `GDataMutex`;
4. executa o ciclo do aimbot quando há dados novos.

O acesso ao processo-alvo é encapsulado em `Memory` e encaminhado ao driver.

### Thread de Render (`OS-ImGui`)

`DiscordOverlay::Run()` prepara o dispositivo DirectX 11, inicializa o ImGui e
executa `RenderThread()`. A cada frame, o callback `RenderFrame()` copia
`GSharedData` para um snapshot local usando `GDataMutex`, calcula a projeção da
câmera quando necessário e desenha a interface e os elementos do overlay.

O renderizador não deve percorrer diretamente estruturas mutáveis do `Engine`;
ele trabalha com o snapshot compartilhado.

## `SharedState.h`

`SharedState.h` define `SharedData`, que contém o estado publicado pela thread
de memória: lista de atores e granadas, cache da câmera, ponteiros globais
relevantes, recuo e metadados da última atualização. Também declara:

- `GSharedData`, o snapshot global;
- `GDataMutex`, o mutex que protege a troca do snapshot;
- `GIsRunning`, a flag de ciclo de vida das threads.

## Fluxo de dados e features

O fluxo lógico principal é:

```text
Driver/Memory -> Engine -> Actors -> SharedState -> ESP -> Radar
                                      └---------> Aimbot
```

- **Engine** resolve o mundo, câmera e estruturas do jogo e coordena as
  leituras de memória;
- **Actors** são descobertos e atualizados pelo `Engine`, incluindo posição,
  equipe, vida, visibilidade e ossos;
- **ESP** (`ESP/PlayerEsp.cpp`) transforma os atores em elementos de tela como
  caixas, esqueletos, nomes, distância e granadas;
- **Radar** (`ESP/Radar.cpp`) usa o mesmo snapshot e a posição da câmera para
  projetar os atores no radar;
- **Aimbot** (`SDK/Aimbot.cpp`) consome o estado atualizado no ciclo de memória
  e aplica as regras configuradas.

Na prática, ESP e Radar são renderizados no frame, enquanto o Aimbot é
acionado após a publicação de uma atualização pelo `MemoryLoop`.

## ReadWriteDriver - Arquitetura Kernel

O transporte de memória usado pelo `PubgExt` é implementado em
`PubgExt/driver/driver_interface_v3.*`. Ele não usa um device handle: a ponte
em user mode resolve `NtUserSetSysColors` em `win32u.dll` e envia a estrutura
`Command` pelo caminho da função hookeada.

O fluxo de carregamento e execução é:

```text
PubgExt (UM) -> driver_interface_v3 -> NtUserSetSysColors -> ReadWriteDriver.sys
```

O mapper carrega `ReadWriteDriverMapper.sys`, que reserva páginas não paginadas
e mapeia manualmente `ReadWriteDriver.sys`. Durante a inicialização, o driver
localiza `win32kbase.sys` e usa o endereço global em
`win32kbase.sys + 0x2B3C90` (associado a `NtUserSetSysColors`) para instalar o
hook. O hook interpreta a `Command`, executa a operação e restaura o fluxo
normal da função original.

### Caminhos alternativos de carga

`tools/kdmapper.exe` é uma opção externa de manual mapping para carregar o
`ReadWriteDriver.sys` sem compilar ou usar o `ReadWriteDriverMapper.sys`
interno. O mapper interno faz parte da arquitetura documentada e do fluxo
oficial do projeto, enquanto o `kdmapper.exe` é um utilitário genérico mantido
fora desse fluxo. A alternativa externa pode ser mais rápida para testes e
quando o mapper não estiver disponível, mas exige execução como administrador,
o modo de testes de assinatura desabilitado e não oferece a mesma integração,
controle de versão ou previsibilidade do mapper interno.

### Command IDs

`PubgExt/driver/common.h` compartilha o layout binário da estrutura com o
driver; a ordem dos campos e os ponteiros de 64 bits precisam permanecer
idênticos. Os comandos são:

- `COMMAND_READWRITE` (`0xB16B00B5`): lê ou escreve memória, conforme `rw`;
- `COMMAND_GETPROCPID` (`0xBADA55`): obtém informações do processo alvo;
- `COMMAND_ISLOADED` (`0x69420`): identifica o estado de carregamento.

As leituras e escritas usam `physmem`: o driver obtém o CR3 do processo, traduz
endereços virtuais para físicos e acessa a memória física em blocos de página.
Isso mantém o caminho de memória separado da camada de renderização e é
encapsulado pela interface `DriverInterfaceV3`.

### Limitações e requisitos

- O deslocamento `win32kbase+0x2B3C90` é hardcoded para Windows 11 build
  `22000.376`; outras versões exigem atualização ou um signature scanner.
- O carregamento de driver exige um ambiente de testes com **test signing**
  habilitado e privilégios apropriados.
- **Secure Boot** pode impedir o carregamento de imagens não assinadas; ele
  precisa ser considerado ao preparar o ambiente de teste.
- O driver e o `PubgExt` são soluções independentes e devem ser compilados e
  validados separadamente.

## Configurações

Os modelos de configuração ficam em `PubgExt/Config/`, com as instâncias
agregadas em `ConfigInstance.h` e serialização em `ConfigUtilities.cpp`.
Durante a execução, a configuração padrão é salva e carregada em:

```text
%USERPROFILE%\Documents\Hunt\Default.json
```

As opções também podem ser alteradas pelo menu ImGui. Novos modelos ou campos
devem ser adicionados aos arquivos de configuração correspondentes e incluídos
na serialização de `ConfigInstances`.

## CI/CD

O workflow `.github/workflows/build.yml` executa o build da solução
`PubgExt.sln` em Windows com `Release|x64` e também valida a configuração do
projeto com `cmake -B build -S .`. Ele é executado em pushes e pull requests
direcionados às branches `main` ou `master`.

## Testes

O carregamento e a validação do driver devem ocorrer somente em uma VM isolada
e restaurável. Consulte o [guia de Teste Seguro do Driver](testing.md) para o
procedimento com Hyper-V, DbgView, Driver Verifier e o fluxo alternativo de
self-hosted runner.

## Como manter atualizado

Ao alterar o SDK ou atualizar a versão do jogo, revise os offsets e regenere o
header correspondente com `tools/dump_offsets.py`. Use a opção `--check` para
verificar se o header está sincronizado antes de abrir um pull request. Depois,
confirme o build local ou acompanhe o workflow `build.yml` para validar a
solução e a configuração CMake.

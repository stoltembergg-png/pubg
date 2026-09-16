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

## Como manter atualizado

Ao alterar o SDK ou atualizar a versão do jogo, revise os offsets e regenere o
header correspondente com `tools/dump_offsets.py`. Use a opção `--check` para
verificar se o header está sincronizado antes de abrir um pull request. Depois,
confirme o build local ou acompanhe o workflow `build.yml` para validar a
solução e a configuração CMake.

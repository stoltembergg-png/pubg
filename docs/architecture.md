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

## ReadWriteDriver — arquitetura device + IOCTL

O transporte de memória usado pelo `PubgExt` é um device WDM legado. O driver
cria o device `\Device\PubgExtRw` e o symlink `\DosDevices\PubgExtRw`, aberto
em user mode como `\\.\PubgExtRw`. O fluxo de dados é:

```text
PubgExt (UM)
  -> CreateFile("\\\\.\\PubgExtRw")
  -> DeviceIoControl (AUTH/CAPS/READ/WRITE)
  -> ReadWriteDriver.sys
  -> cópia virtual com attach ao processo
  -> processo-alvo
```

O device é criado com `IoCreateDeviceSecure`, `FILE_DEVICE_SECURE_OPEN` e o
SDDL `D:P(A;;GA;;;SY)(A;;GA;;;BA)`. Apenas `SYSTEM` e `Administrators` têm
acesso; portanto, o app precisa ser executado elevado. Não há fallback para
uma ACL insegura.

O mapper carrega `ReadWriteDriverMapper.sys`, que mapeia manualmente a imagem
do payload. A ordem real do preflight é: obter a versão do sistema e comparar o
build com a versão do perfil; recusar HVCI ativo; localizar o
`ntoskrnl` carregado; validar sua identidade (TDS, `SizeOfImage`, checksum,
GUID+Age do PDB e tamanho do arquivo); confirmar que existe um perfil gerado;
validar que os três RVAs do perfil estão em seções executáveis; e só então
validar o layout PE do payload e o entry point. Depois da alocação e da
proteção da imagem, o mapper copia headers e seções, aplica relocations,
resolve a IAT e chama o entry point. Em uma falha de mapeamento, a alocação da
imagem é liberada; a existência e o ciclo de vida do device precisam continuar
sendo validados em VM.

`tools/profiles/extract_profile.py` gera, a partir dos binários de uma máquina,
um JSON de evidências e `tools/profiles/generated/profiles_generated.h`. O
header gerado é a fonte única consumida pelo mapper. Para o perfil atual, os
RVAs usados pelo mapper são `MmAllocateIndependentPages` `0xAA42A0`,
`MmSetPageProtection` `0x4E5EE0` e `MmFreeIndependentPages` `0x2065C0`.
Cada máquina e cada atualização do Windows exigem um novo perfil revisado; isso
não é suporte genérico por build nem permite nearest-match.

O perfil/tooling também carrega identidades de `win32kbase`, `win32kfull` e
`win32k`, além de campos de `EPROCESS`. Esses dados são evidência da análise de
RE e podem aparecer no JSON/header, mas não são gate nem dependência de runtime
do transporte atual; o mapper usa a identidade do `ntoskrnl` e os três RVAs
gerados acima.

### Caminhos alternativos de carga

`tools/kdmapper-src/` é a fonte vendorada do loader upstream
`TheCruZ/kdmapper`, commit
`48ac931d87372702a23c6f34ee7b8440787d9fc7`, sob MIT, com atribuição em
`tools/kdmapper-src/VENDORING.md`. A fonte compila em `Release|x64`; o artefato
registrado tem 154112 bytes. Seus padrões e assinaturas foram validados para o
kernel local `10.0.26100.9457`, incluindo as tabelas de PiDDB, WdFilter e a
lista de hashes de CI descritas em `VENDORING.md`. A fonte é a referência
auditável para build e correções. `tools/kdmapper.exe` é apenas o binário
legado, opaco e não auditado.

Esse loader é o vetor BYOVD que usa `iqvw64e.sys` v1.03.0.7. A rota externa
exige blocklist de drivers vulneráveis desabilitada e execução como
administrador, somente em VM autorizada e isolada. No fluxo do projeto, ele
carrega o `ReadWriteDriverMapper.sys`; esse mapper interno é quem mapeia o
payload e aplica o ABI do device. A rota externa não substitui a validação do
mapper interno nem transforma a cobertura local em suporte genérico para outras
builds.

### Protocolo IOCTL e autenticação

O contrato canônico está em `PubgExt/driver/ioctl_protocol.h` e usa
`stdint.h`/tipos de tamanho fixo, com `static_assert` para tamanhos e offsets.
Todos os IOCTLs usam `METHOD_BUFFERED`, `DeviceType 0x8337` e protocolo major
2, minor 0. Os códigos de função são `AUTH` (`0x800`), `QUERY_CAPS`
(`0x801`), `READ` (`0x802`) e `WRITE` (`0x803`).

Os layouts públicos são:

- `RequestHeader`: 32 bytes;
- `ResponseHeader`: 40 bytes;
- request de READ: 56 bytes;
- request fixo de WRITE: 56 bytes, seguido pelos dados;
- response de `QUERY_CAPS`: 48 bytes.

O magic é `0x50554247`, o limite por operação é 16 MiB e a cópia é feita em
chunks de 64 KiB. Major incompatível retorna `STATUS_REVISION_MISMATCH`.
Não existe `Command` de 96 bytes, token devolvido pelo driver ou autorização
por PID.

No `IRP_MJ_CREATE`, o driver cria uma sessão por `FILE_OBJECT`. O opener deve
estar em `UserMode`; o processo é obtido com `IoGetRequestorProcess`, seu
caminho completo de imagem é resolvido e comparado à allowlist. Falha ao
resolver o caminho também falha a abertura, sem fallback para basename. O
`PEPROCESS` é referenciado pela sessão e comparado em cada IOCTL.

O `DispatchDeviceControl` primeiro rejeita IOCTL desconhecido com
`STATUS_INVALID_DEVICE_REQUEST`. Para um código conhecido, verifica
`RequestorMode`, adquire a sessão sob `KeEnterCriticalRegion` e push lock
pareados, confere a sessão e a identidade do chamador e exige o buffer fixo
mínimo. Em `AUTH` e `QUERY_CAPS`, valida então o header na ordem magic,
major/minor, `struct_size`, flags e `reserved`, e só depois os tamanhos exatos
do IRP. Em `READ`, verifica primeiro o tamanho exato da entrada, depois o
header, os limites/tamanhos da saída, campos reservados, PID e faixa de VA, e
por fim faz o lookup do processo. Em `WRITE`, verifica primeiro o mínimo do
payload fixo, depois o header, o tamanho fixo mais os dados, a saída, campos
reservados, PID e faixa de VA, e então o lookup. No lookup, o processo é
referenciado e um alvo WOW64 é rejeitado; WOW64 é rejeitado pelo protocolo v2.

Também são checados os overflows de `addr + len` e de `fixed + len`. VA de
kernel é rejeitada (o último byte deve ser menor ou igual a
`MM_HIGHEST_USER_ADDRESS`). Depois que os campos básicos tornam uma resposta
possível, falhas de lookup são refletidas em `operation_status`.

### Cópia virtual

READ e WRITE usam cópia virtual em
`ReadWriteDriver/ReadWriteDriver/virtual_copy.c`: `KeStackAttachProcess`,
`ProbeForRead`/`ProbeForWrite` e cópia em `PASSIVE_LEVEL`. A referência obtida
por `PsLookupProcessByProcessId` é propriedade explícita de
`VirtualCopyProcess`: o callee a consome em `__finally`, inclusive em retornos
antecipados, e garante detach quando o attach ocorreu. A cópia divide cada
chunk de 64 KiB nas fronteiras de página; `transferred` conta exatamente os
bytes concluídos até a página que falhou. Não há page-walking físico, acesso a
CR3 ou offsets de `EPROCESS` no caminho de runtime.

### Remoções arquiteturais

Foram removidos o transporte anterior, `physmem.c/.h`, `assembly.asm`, process
notify, work item, rundown do transporte, token de sessão, `user_result`, o
acesso por CR3 e o handshake por PID no mapper. Isso não significa que toda
evidência de offsets foi apagada: o perfil/tooling ainda registra as identidades
dos módulos gráficos e campos de `EPROCESS` para análise de RE, sem usá-los como
gate ou dependência do runtime. PID e base de módulo são obtidos pelo app com
Toolhelp32; não pelo driver.

### Plataforma, limitações e requisitos

- O suporte é definido pelo **perfil exato da identidade do `ntoskrnl`**, não
  por um número genérico de build do Windows. O perfil atual identifica o kernel
  `10.0.26100.9457`; o host do owner é build `26200.9457`.
- O carregamento exige um ambiente de testes autorizado, test signing quando
  aplicável e privilégios apropriados. O app também precisa de elevação para
  abrir o device.
- **Secure Boot** pode impedir o carregamento de imagens não assinadas; ele
  precisa ser considerado ao preparar o ambiente de teste.
- `DriverUnload = NULL`: hot-unload não é suportado por desenho. A imagem
  permanece até o reboot; feche o app para encerrar a sessão, mas não tente
  descarregar o driver.
- O driver e o `PubgExt` são soluções independentes e devem ser compilados e
  validados separadamente.

## Lifecycle do app (user-mode)

O encerramento usa uma flag de parada atômica, uma thread joinável e shutdown
ordenado, sem `detach`. Snapshots de `SharedData` são feitos por valor, com
cópia profunda sob `GDataMutex`; `BaseLog` é `thread_local`.

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

## Riscos e limitações

Consulte o documento central de riscos residuais em
[`docs/known-issues.md`](known-issues.md), incluindo o que ainda não foi
validado em VM e a proibição de hot-unload.

## Como manter atualizado

Ao mudar a máquina ou a atualização do Windows, obtenha um novo dump e gere um
perfil com `tools/profiles/extract_profile.py`. Cada máquina/atualização exige
um perfil novo; não selecione o perfil mais próximo. Depois de recompilar o
driver, regenere o payload e confirme o build local conforme o [guia de
teste](testing.md).

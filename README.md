# PubgExt

Overlay baseado em ImGui para visualização de informações do jogo e leitura de
memória por meio de um driver de kernel. O projeto é voltado a ambientes de
teste autorizados e depende da versão correspondente do jogo, dos offsets e do
driver.

## Stack

- C++20
- MSVC v143 (Visual Studio 2022)
- ImGui com backend DirectX 11/Win32
- Solução x64 para Windows

## Pré-requisitos

- Visual Studio 2022 com o workload **Desenvolvimento para Desktop com C++**;
- Windows 10 SDK 10.0 instalado pelo Visual Studio Installer;
- Windows Driver Kit (WDK) correspondente ao Windows SDK, para compilar o
  driver;
- Driver de kernel compatível, carregado e configurado para o ambiente de teste;
- execução do app como administrador: o device `\\.\PubgExtRw` aceita somente
  `SYSTEM` e `Administrators`;
- Discord instalado e com o overlay habilitado, caso o renderizador do Discord
  seja utilizado.

## Plataforma suportada

O suporte é definido pelo **perfil exato da identidade do `ntoskrnl`**, não por
um número genérico de build. O perfil atual é o kernel `10.0.26100.9457` no
host build `26200.9457`; o payload e o mapper rejeitam identidades sem perfil
exato. Cada máquina e cada atualização exigem um perfil novo; o projeto não é
suporte genérico para builds não perfiladas.

## Como buildar

1. Abra `PubgExt.sln` no Visual Studio 2022.
2. Selecione a plataforma `x64` e a configuração `Release`.
3. Compile a solução (**Build > Build Solution**).

O carregamento do driver e a execução do binário devem ser feitos somente em
um ambiente autorizado e compatível com a configuração usada no build.

## Driver

O driver possui uma solução própria com três projetos (`ReadWriteDriver`,
`ReadWriteDriverMapper` e `ReadWriteUser`). Compile-a separadamente da aplicação
principal:

```text
msbuild ReadWriteDriver/ReadWriteKernel.sln /p:Configuration=Release /p:Platform=x64
```

`ReadWriteKernel.sln` não faz parte do CMake nem da solução `PubgExt.sln`; o
WDK deve estar instalado para que os projetos de kernel sejam reconhecidos e
compilados. As duas solutions têm configurações e artefatos independentes.

Os arquivos `.sys` não são versionados no repositório. É necessário compilá-los
com o WDK e regenerar o payload após cada recompilação do driver:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ReadWriteDriver/tools/embed_payload.ps1
```

O script usa, por padrão, `ReadWriteDriver/x64/Release/ReadWriteDriver.sys`.

O driver expõe `\\.\PubgExtRw` por `CreateFile`/`DeviceIoControl` para AUTH,
CAPS, READ e WRITE. `DriverUnload = NULL`: hot-unload não é suportado; feche o
app e reinicie a máquina quando for necessário encerrar o ciclo do driver.

Ao atualizar a máquina ou o Windows, faça um novo perfil com
`tools/profiles/extract_profile.py`, compile o driver para essa identidade e
regenere o payload. Não use o perfil mais próximo. Para o procedimento de
validação do loader e do driver, consulte [`docs/testing.md`](docs/testing.md).

## Estrutura de pastas

- `PubgExt/`: código principal da aplicação;
- `PubgExt/Memory/`: acesso e sincronização da leitura de memória;
- `PubgExt/SDK/`: estruturas do engine, atores, câmera e aimbot;
- `PubgExt/ESP/`: ESP e radar;
- `PubgExt/OS-ImGui/`: camada de renderização ImGui/DirectX;
- `PubgExt/Config/`: modelos e utilitários de configuração;
- `PubgExt/driver/`: interface com o driver de kernel;
- `Include/` e `Lib/`: diretórios reservados para headers e bibliotecas locais;
- `docs/`: documentação técnica do projeto;
- `tools/profiles/`: extrator e perfis gerados de identidade do kernel;
- `tools/kdmapper-src/`: fonte vendorada e auditável do loader;
- `tools/`: ferramentas auxiliares, incluindo `tools/dump_offsets.py`.

## Perfil do kernel e loader

Execute `tools/profiles/extract_profile.py` na própria máquina testada para
gerar o JSON de identidades (TDS, `SizeOfImage`, checksum e GUID+Age) e os RVAs
de `ntoskrnl` usados pelo mapper. Revise o JSON e regenere
`tools/profiles/generated/profiles_generated.h`; esse header é a fonte única
compilada pelo mapper. As identidades de `win32kbase`, `win32kfull`, `win32k` e
campos de `EPROCESS` que também aparecem no perfil são evidência de RE, não gate
nem dependência de runtime.

O loader de referência é a fonte vendorada em `tools/kdmapper-src/`, upstream
`TheCruZ/kdmapper` no commit
`48ac931d87372702a23c6f34ee7b8440787d9fc7`, sob MIT, com atribuição em
`tools/kdmapper-src/VENDORING.md`. A fonte compila em Release x64 e o artefato
registrado tem 154112 bytes; os padrões foram validados para o kernel local
`10.0.26100.9457`, incluindo PiDDB, WdFilter e a lista de hashes de CI. O
loader é o vetor BYOVD via `iqvw64e.sys` v1.03.0.7 e exige blocklist de drivers
vulneráveis desabilitada e administrador, somente em VM autorizada. O
`tools/kdmapper.exe` permanece como binário legado não auditado; use a fonte
como referência para build e correção.

## Atualização de offsets

Offsets precisam acompanhar a versão do jogo e ser validados antes de qualquer
execução. Use `tools/dump_offsets.py` como ponto de partida para gerar ou
atualizar os dados de offsets e revise os valores consumidos pelo SDK antes de
compilar novamente.

## Notas sobre o Discord overlay

O renderizador procura a janela existente do Discord Overlay e a utiliza como
superfície de renderização. Mantenha o overlay do Discord habilitado e o jogo
em modo **borderless**. Se a janela não estiver disponível, a inicialização do
renderizador aguardará até encontrá-la.

## Aviso legal

Use este projeto somente em software, contas e ambientes para os quais você
tenha autorização. Você é responsável por cumprir os termos de serviço, leis e
políticas aplicáveis; não há autorização para burlar anti-cheat ou acessar
dados de terceiros.

## Riscos conhecidos

Consulte [`docs/known-issues.md`](docs/known-issues.md) antes de qualquer teste
do driver, especialmente sobre validação em VM e a proibição de unload.

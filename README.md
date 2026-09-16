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
- Discord instalado e com o overlay habilitado, caso o renderizador do Discord
  seja utilizado.

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
- `tools/`: ferramentas auxiliares, incluindo `tools/dump_offsets.py`.

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

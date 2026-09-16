# Changelog

Todas as mudanças relevantes deste projeto serão documentadas neste arquivo.

O formato segue as convenções do [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/),
e o projeto segue versionamento semântico.

## [0.3.0] - 2026-09-16

### Alterado

- offsets sincronizados com o SDK 2609.1.1.93 a partir da fonte única
  `Config/Offsets.h` (182 offsets), incluindo `CameraCacheFOV` de `0x468` para
  `0x478` e `Decrypt` de `0x1079C028` para `0x107A7828`;
- lifecycle user-mode seguro, com parada atômica, thread joinável e shutdown
  ordenado;
- suíte GoogleTest + CTest;
- CI com 4 jobs: offsets `--check` bloqueante, guard de payload, build CMake
  real e driver não bloqueante com aviso;
- hardening do driver: autenticação por `PEPROCESS`, validação, limites,
  máscaras físicas, gate de build e teardown com dono único;
- payload regenerado e byte-idêntico ao `.sys`;
- plataforma restrita ao Windows 10 21H1, build 19043.

### Documentação

- problemas abertos e riscos residuais referenciados em
  [`docs/known-issues.md`](docs/known-issues.md).

## [0.2.0] - 2026-09-16

### Adicionado

- **ReadWriteDriver:** integração dos projetos de driver, mapper e user mode
  para leitura e escrita de memória;
- **Ponte `driver_interface_v3`:** transporte do `PubgExt` para o hook
  `NtUserSetSysColors`, com a estrutura compartilhada em
  `command_protocol.h`.

## [0.1.0] - 2026-09-16

### Adicionado

- **P0:** base do SDK, integração com o driver e fluxo principal de leitura e
  renderização.
- **P1:** estado compartilhado entre as threads, organização das features de
  ESP, radar e aimbot, além da ferramenta de atualização de offsets.
- **P2:** documentação de arquitetura, workflow de build para Windows, template
  de pull request e instruções de manutenção do projeto.

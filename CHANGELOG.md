# Changelog

Todas as mudanças relevantes deste projeto serão documentadas neste arquivo.

O formato segue as convenções do [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/),
e o projeto segue versionamento semântico.

## [0.2.0] - 2026-09-16

### Adicionado

- **ReadWriteDriver:** integração dos projetos de driver, mapper e user mode
  para leitura e escrita de memória;
- **Ponte `driver_interface_v3`:** transporte do `PubgExt` para o hook
  `NtUserSetSysColors`, com a estrutura compartilhada em `common.h`.

## [0.1.0] - 2026-09-16

### Adicionado

- **P0:** base do SDK, integração com o driver e fluxo principal de leitura e
  renderização.
- **P1:** estado compartilhado entre as threads, organização das features de
  ESP, radar e aimbot, além da ferramenta de atualização de offsets.
- **P2:** documentação de arquitetura, workflow de build para Windows, template
  de pull request e instruções de manutenção do projeto.

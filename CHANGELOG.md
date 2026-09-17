# Changelog

Todas as mudanças relevantes deste projeto serão documentadas neste arquivo.

O formato segue as convenções do [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/),
e o projeto segue versionamento semântico.

## [0.5.0] - 2026-09-17

### Alterado

- migração device + IOCTL consolidada no transporte atual;
- correção P0 do mapper: headers PE são copiados antes de resolver IAT e
  relocations;
- correções P1 do driver, incluindo IOCTL desconhecido, allowlist canonicalizada,
  locks pareados e preenchimento de `operation_status`;
- `virtual_copy` corrigido para probe somente no remoto, posse explícita da
  referência de processo e contagem de bytes exata por página;
- testes do protocolo IOCTL e dos layouts do ABI;
- vendorização do kdmapper com fonte auditável e cobertura estática validada
  para o kernel local `10.0.26100.9457`;
- toolkit de perfis por identidade, com geração de JSON e
  `profiles_generated.h` para os RVAs usados pelo mapper.

### Segurança e limitações

- a fonte vendorada do loader é a referência de build e correção; o binário
  legado `tools/kdmapper.exe` permanece não auditado;
- o driver continua sem hot-unload e os testes de carga permanecem restritos a
  VM autorizada e restaurável.

## [0.4.0] - 2026-09-16

### Alterado

- migração do transporte para device WDM legado `\\.\PubgExtRw` e IOCTLs
  `AUTH`, `QUERY_CAPS`, `READ` e `WRITE`;
- contrato canônico de protocolo major 2, com validação fail-closed, limites e
  testes dos layouts e das operações;
- cópia de memória virtual com attach ao processo, probes e cleanup garantido;
- remoção do transporte anterior, page-walking físico, CR3, process notify,
  work item e handshake por PID no mapper;
- suporte selecionado por perfil exato da identidade do `ntoskrnl`, com
  preflight de HVCI/CI e RVAs gerados;
- documentação de arquitetura, problemas conhecidos e fluxo de teste
  atualizados para o novo contrato.

### Segurança e limitações

- o device exige app elevado e o driver não oferece hot-unload; a imagem
  permanece até reboot por desenho;
- riscos residuais e comportamentos ainda não validados estão em
  [`docs/known-issues.md`](docs/known-issues.md).

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
- plataforma histórica substituída pelo suporte por perfil da versão 0.4.0.

### Documentação

- problemas abertos e riscos residuais referenciados em
  [`docs/known-issues.md`](docs/known-issues.md).

## [0.2.0] - 2026-09-16

### Adicionado

- **ReadWriteDriver:** integração histórica dos projetos de driver, mapper e
  user mode para leitura e escrita de memória;
- **Ponte `driver_interface_v3` (histórica, substituída na 0.4.0):** contrato
  legado do transporte, não usado pelo mecanismo vigente.

## [0.1.0] - 2026-09-16

### Adicionado

- **P0:** base do SDK, integração com o driver e fluxo principal de leitura e
  renderização.
- **P1:** estado compartilhado entre as threads, organização das features de
  ESP, radar e aimbot, além da ferramenta de atualização de offsets.
- **P2:** documentação de arquitetura, workflow de build para Windows, template
  de pull request e instruções de manutenção do projeto.

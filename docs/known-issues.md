# Problemas conhecidos e riscos residuais

## Contexto

O transporte atual usa device + IOCTL e cópia virtual. Esta lista separa as
correções concluídas nesta rodada dos pontos que ainda exigem execução em VM.
O hot-unload permanece proibido por desenho.

## Fechado nesta rodada

- O mapper agora copia os headers PE antes de resolver IAT e relocations. Antes,
  essas rotinas consultavam diretórios no destino ainda sem headers copiados, o
  que deixava imports e relocations sem fixup.
- IOCTL desconhecido retorna `STATUS_INVALID_DEVICE_REQUEST`.
- A allowlist canonicaliza caminhos nativos e DOS, aceita o app
  `RuntimeBroker.exe` e o cliente de teste `ReadWriteUser.exe`, e não usa
  fallback por basename.
- Todas as aquisições de push lock têm `KeEnterCriticalRegion` e
  `KeLeaveCriticalRegion` pareados.
- Falhas de lookup preenchem `operation_status` quando a resposta já é válida.
- `VirtualCopyProcess` tem posse explícita da referência de `PEPROCESS`: o
  callee consome a referência inclusive em retornos antecipados, faz detach
  quando necessário e reporta `transferred` com granularidade por página.
- A cobertura offline do loader vendorado foi registrada em
  [`tools/kdmapper-src/VENDORING.md`](../tools/kdmapper-src/VENDORING.md) para
  o kernel local `10.0.26100.9457`; isso não substitui o teste de execução.

## Riscos abertos e validação obrigatória em VM

Os pontos abaixo continuam residuais e não devem ser tratados como validados
apenas por compilação ou inspeção estática:

- ACL/`IoCreateDeviceSecure` a partir de uma imagem mapeada manualmente;
- `IRP_MJ_CREATE` e a identidade real do opener;
- attach + probe sob alvos reais, incluindo páginas inválidas, operações
  cross-page e processo encerrando;
- semântica de partial copy;
- concorrência de IOCTLs;
- HVCI ativo: o preflight deve recusar o carregamento, e esse teste negativo
  ainda precisa ser executado;
- PatchGuard, CFG e Driver Verifier;
- identidade e RVAs do `ntoskrnl` em outra build/perfil;
- comportamento do exploit `iqvw64e.sys` v1.03.0.7 sob blocklist de drivers
  vulneráveis, antivírus/EDR e anti-cheat.

O loader vendorado foi validado por varredura estática somente para os arquivos
locais descritos no `VENDORING.md`; não houve invocação do loader, carga do
driver vulnerável, mapeamento ou execução do payload durante essa auditoria.
`tools/kdmapper.exe` continua sendo o binário legado não auditado; a fonte em
`tools/kdmapper-src/` é a referência para build e correção.

## Lifetime e unload

O mapper libera a imagem alocada quando o mapeamento falha e o payload exclui o
device se a criação do symlink falhar. A referência de processo e o attach da
cópia virtual têm cleanup explícito nos caminhos implementados. Isso não é uma
garantia geral para todo o ciclo de vida em execução: device, dispatch, imagem
mapeada e comportamento após falhas ainda precisam da validação em VM listada
acima.

`DriverUnload = NULL`: **não descarregue o driver**. Hot-unload não é suportado;
a imagem permanece até o reboot. Para limpar um teste, feche o app e reinicie
ou restaure o checkpoint da VM. Não transforme a ausência de unload em uma
correção local sem redesenho explícito do lifetime.

## Artefatos

Os arquivos `.sys` não são versionados. Compile com o WDK e regenere o payload
após cada recompilação do driver; mantenha perfil, driver, mapper e hashes do
mesmo build juntos. Consulte [`ReadWriteDriver/README.md`](../ReadWriteDriver/README.md)
e [`docs/testing.md`](testing.md) para o fluxo autorizado.

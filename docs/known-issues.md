# Problemas conhecidos e riscos residuais

## Contexto

O driver passou por 6 iterações de hardening, mas foi reprovado por 5 revisões
independentes. O resíduo descrito abaixo exige **redesenho arquitetural**, não
um patch incremental. Portanto, os itens permanecem bloqueadores para uso
confiável do teardown e do unload.

## Problemas abertos

### CRÍTICO — Conclusão falsa do teardown

- **Impacto:** o evento de conclusão do work item é sinalizado dentro do próprio
  callback, antes de ele de fato retornar. Além disso, existe uma janela entre
  publicar o estado `REVOKING` e resetar o evento: um sinal antigo pode ser
  consumido, levando a `IDLE` prematuro e ao reuso/liberação prematura da
  imagem. Isso pode causar use-after-free ou BSOD ao descarregar o driver ou
  quando o processo autorizado encerra.
- **Mitigação provisória:** **não descarregar o driver durante o uso**.
- **Status:** aberto; requer redesenho do protocolo de conclusão e teardown.

### CRÍTICO — Hook e epílogo dentro da imagem liberável

- **Impacto:** o hook e seu epílogo executam dentro da imagem que pode ser
  liberada. O rundown é liberado antes do retorno real do hook, e o hook
  executa antes da aquisição da proteção necessária.
- **Mitigação/correção robusta necessária:** usar um trampoline e objetos de
  sincronização em memória estável do mapper, mantendo a proteção enquanto o
  payload é chamado e sinalizando somente depois que a chamada ao payload
  retornar.
- **Status:** não implementado.

### ALTO — Ausência de `__finally` na execução completa do hook

- **Impacto:** não há `__finally` envolvendo a execução completa do hook; uma
  exceção não tratada entre a aquisição e a liberação pode vazar a contagem do
  rundown.
- **Mitigação:** não considerar o teardown seguro até que a execução completa
  esteja protegida por cleanup garantido.
- **Status:** aberto; faz parte do redesenho necessário.

## Não validado em VM

Os seguintes comportamentos ainda não foram validados em uma VM isolada:

- handshake real e semântica de retorno de `NtUserSetSysColors`;
- contexto, IRQL e `PreviousMode` no ponto do hook;
- aceitação e lifetime do process notify vindo de imagem mapeada manualmente;
- PatchGuard, HVCI, CFG e Driver Verifier;
- offsets de `win32k`/`ntoskrnl` na build alvo;
- tradução física sob remapeamento, paginação e encerramento de processo;
- corridas de teardown/revogação sob concorrência;
- unload real.

## Artefatos e validações conhecidas

- O `hexData` do payload tem 13824 bytes, SHA-256
  `C5390F4BBDC28A7A02C0E0D29E86E4A69288DD23FE41A7223A846FB59FB62E61` e é
  byte-idêntico (0 divergências) a
  `ReadWriteDriver/x64/Release/ReadWriteDriver.sys`.
- `ReadWriteDriverMapper.sys` tem 22016 bytes.
- O build Release do app e o `ctest` (1/1) estão verdes; `tools/dump_offsets.py
  --check` retorna `up to date` para 182 offsets.

Essas validações não eliminam os riscos de teardown nem substituem a validação
em VM.

## Aviso de artefatos

Os arquivos `.sys` **não são versionados no repositório**. Eles precisam ser
compilados com o WDK e o payload embutido precisa ser regenerado antes do uso.
Consulte as instruções em [`ReadWriteDriver/README.md`](../ReadWriteDriver/README.md).

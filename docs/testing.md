# Teste Seguro do Driver

Este guia descreve um procedimento de teste isolado para o
`ReadWriteDriver`. Use-o somente em máquinas, contas, jogos e ambientes para
os quais você tenha autorização. O procedimento não é apropriado para o host
principal nem para uma máquina que contenha dados de trabalho.

## Por que não testar no host principal

Um driver de kernel com perfil incompatível ou erro de inicialização pode causar
**BSOD**, perda de estado e reinicializações. Além
disso, o Windows pode impedir o carregamento por causa do **HVCI/Memory
Integrity**, do Secure Boot ou da Vulnerable Driver Blocklist. Desabilitar essas
proteções reduz a segurança da máquina; por isso, faça os testes em uma VM
descartável, com snapshot e sem credenciais ou arquivos importantes.

## Preparar uma VM no Hyper-V

1. Crie uma VM Generation 2 no Hyper-V com uma instalação limpa compatível com
   o perfil atual: kernel `10.0.26100.9457` em host build `26200.9457`.
   Reserve CPU e memória suficientes e mantenha a rede desnecessária
   desconectada.
2. Antes de qualquer alteração, desligue a VM e crie um **snapshot/checkpoint**
   chamado, por exemplo, `win10-driver-clean`.
3. Nas configurações de firmware da VM, desative o **Secure Boot**. Inicialize
   a VM e, em *Windows Security > Device security > Core isolation*, desative
   **Memory integrity (HVCI)**. Reinicie quando solicitado.
4. Em um PowerShell ou Prompt de Comando executado como administrador,
   desabilite a blocklist de drivers vulneráveis para esta VM de teste:

   ```text
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Config" /v VulnerableDriverBlocklistEnable /t REG_DWORD /d 0 /f
   ```

   Reinicie a VM para que a alteração seja aplicada.
5. Ainda em um terminal elevado, habilite o test signing e reinicie:

   ```text
   bcdedit /set testsigning on
   shutdown /r /t 0
   ```

   Confirme depois do boot que o Windows está em modo de teste. Se o
   `bcdedit` indicar que a alteração foi bloqueada, verifique novamente o
   Secure Boot e não tente contornar a proteção no host.
6. Configure o **Driver Verifier** somente para os binários em teste. Por
   exemplo, após copiar o driver para `C:\DriverTest`, em terminal elevado:

   ```text
   verifier /standard /driver ReadWriteDriver.sys ReadWriteDriverMapper.sys
   ```

   Reinicie se o Verifier solicitar. Não selecione todos os drivers do sistema.
   Para desativá-lo após o teste, use:

   ```text
   verifier /reset
   shutdown /r /t 0
   ```

   O Verifier pode provocar um BSOD intencionalmente ao detectar uma violação;
   isso é esperado somente dentro da VM com checkpoint.

## Perfil, build e payload

O alvo conhecido é o perfil exato do kernel `10.0.26100.9457` (host build
`26200.9457`). O perfil não é um suporte genérico por build: na máquina que
será testada, gere um novo perfil a partir dos binários locais:

```powershell
py -m pip install pefile capstone requests pdbparse
py tools/profiles/extract_profile.py --system-dir C:\Windows\System32
```

Revise o JSON em `tools/profiles/generated/` e confirme TDS, `SizeOfImage`,
checksum e GUID+Age das identidades, além dos três RVAs de `ntoskrnl` usados
pelo mapper. Confirme que `profiles_generated.h` foi regenerado e será a fonte
única compilada pelo mapper; não reutilize o perfil de outra máquina ou
atualização e não faça nearest-match.

Após essa verificação, recompile o driver com o WDK e regenere o payload antes
de copiar os artefatos para a VM:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ReadWriteDriver/tools/embed_payload.ps1
```

O caminho canônico do driver recompilado é
`ReadWriteDriver/x64/Release/ReadWriteDriver.sys`. Confira também o mapper
correspondente e os hashes antes do teste.

### Validação do loader vendorado

Compile a fonte auditável do loader e registre o resultado:

```powershell
MSBuild tools\kdmapper-src\kdmapper.sln /m /t:Build /p:Configuration=Release /p:Platform=x64 /nologo
```

Confirme o artefato `tools/kdmapper-src/x64/Release/kdmapper_Release.exe` e o
tamanho registrado de 154112 bytes. Revise a tabela de padrões/assinaturas em
`tools/kdmapper-src/VENDORING.md`: a cobertura estática registrada é para o
kernel local `10.0.26100.9457`, incluindo PiDDB, WdFilter e a lista de hashes
de CI. Esse loader usa o vetor BYOVD `iqvw64e.sys` v1.03.0.7 e exige blocklist
de drivers vulneráveis desabilitada e terminal elevado; portanto, qualquer
execução dele ocorre somente na VM autorizada, nunca no host principal.

`tools/kdmapper.exe` não é substituto dessa validação: é o binário legado não
auditado. A fonte vendorada é a referência para build e correções.

## Fluxo de teste

1. Desligue a VM e crie um checkpoint imediatamente antes do carregamento.
   Nunca teste sobre o checkpoint limpo; mantenha um ponto para o qual seja
   possível retornar.
2. Copie para a VM, por um meio controlado, os artefatos correspondentes ao
   mesmo build: `ReadWriteDriverMapper.sys`, o payload regenerado e a fonte
   compilada `tools/kdmapper-src/x64/Release/kdmapper_Release.exe` quando a
   rota externa for validada. Não use `tools/kdmapper.exe` para substituir a
   fonte auditável.
3. Para validar a rota externa do loader, abra um terminal como administrador
   no diretório dos artefatos e execute o comando compatível com o artefato
   escolhido:

    ```text
    kdmapper_Release.exe ReadWriteDriverMapper.sys
    ```

   O comando faz manual mapping pelo loader vendorado; não instale o driver
   como serviço e não execute o loader no host principal. O mapper interno e a
   rota externa não são equivalentes: valide também o fluxo interno, usando
   `ReadWriteDriverMapper.sys` conforme o build do projeto, sem confundir os
   dois artefatos.
4. Abra o **DbgView** como administrador antes ou logo após o comando e
   observe as mensagens do driver. Registre a saída, a versão do Windows, os
   hashes dos binários e qualquer erro de inicialização.
5. Execute o app **como administrador**. A ACL do device permite somente
   `SYSTEM` e `Administrators`; uma execução não elevada deve falhar no
   `CreateFile`.
6. Com o app elevado, valide o fluxo de protocolo nesta ordem:
   - `CreateFile("\\\\.\\PubgExtRw")`;
   - `QUERY_CAPS` e `AUTH` bem-formados;
   - READ/WRITE pequeno em um processo autorizado;
   - leitura/escrita cross-page;
   - VA inválida;
   - VA de kernel, que deve ser rejeitada;
   - processo encerrando durante a operação;
   - caso de partial copy.
7. Em uma VM separada ou após restaurar o checkpoint, mantenha HVCI ativo e
   confirme o teste negativo: o preflight deve recusar o carregamento.
8. Se ocorrer BSOD, travamento, reinicialização ou comportamento inesperado,
   não repita o carregamento automaticamente: desligue a VM e reverta para o
   checkpoint anterior. Preserve os dumps para diagnóstico.
9. Ao terminar, feche o app para encerrar a sessão. **Não descarregue o
   driver:** hot-unload não é suportado e a imagem permanece até o reboot.
   Desative o Driver Verifier, reverta as alterações de
   test-signing/blocklist quando possível e restaure o checkpoint. Não deixe a
   VM de uso geral com essas proteções desabilitadas.

## Alternativa: self-hosted runner

Um runner self-hosted pode ser usado quando a execução precisa ocorrer em uma
máquina Windows dedicada. Ele deve ser uma VM descartável ou um computador
isolado, sem acesso a segredos de produção. Nunca use o computador pessoal como
runner para testes de kernel.

No GitHub, configure o runner em:

**Settings → Actions → Runners → New self-hosted runner**

Durante o registro, atribua labels explícitas, por exemplo `self-hosted`,
`windows` e `driver-vm`. Restrinja o acesso do repositório e remova o runner
quando a VM for descartada. Um job de teste deve exigir a label específica e
permitir falha sem bloquear o build principal; o trecho abaixo é apenas um
exemplo documental e não altera `.github/workflows/build.yml`:

```yaml
driver-vm-test:
  runs-on: [self-hosted, windows, driver-vm]
  continue-on-error: true
  steps:
    - name: Teste manual autorizado
      run: .\tools\run-driver-test.ps1
```

O script deve ser revisado e aprovado antes da execução, não deve receber
segredos por argumentos e deve restaurar o snapshot mesmo quando o teste
falhar. `continue-on-error` torna o job informativo; não transforma uma falha
de kernel em um resultado seguro.

## Checklist pré-teste

- [ ] A VM está desligada ou restaurável a um checkpoint limpo recente.
- [ ] O teste está autorizado e não usa o host principal.
- [ ] A identidade do `ntoskrnl` é exatamente `10.0.26100.9457`, no host build
      `26200.9457`, e corresponde ao perfil usado no mapper.
- [ ] Secure Boot e Memory Integrity/HVCI estão desativados somente na VM.
- [ ] `VulnerableDriverBlocklistEnable` está em `0` somente na VM de teste.
- [ ] `bcdedit /set testsigning on` foi aplicado e a VM foi reiniciada.
- [ ] O Driver Verifier está configurado apenas para os drivers em teste.
- [ ] O antivírus/EDR da VM foi desativado temporariamente, somente conforme a
      política do ambiente, e será reativado ao terminar.
- [ ] O perfil foi extraído para esta máquina/atualização; não reutilize perfil
      de outra identidade nem faça nearest-match.
- [ ] `ReadWriteDriverMapper.sys`, o driver necessário e `kdmapper.exe` foram
      obtidos do build esperado e tiveram seus hashes conferidos.
- [ ] O app será executado elevado e o fluxo `CreateFile`/CAPS/AUTH/READ/WRITE
      foi planejado.
- [ ] O teste negativo com HVCI ativo está planejado em VM separada.
- [ ] O driver não será descarregado; o cleanup será feito fechando o app e
      reiniciando/restaurando a VM.
- [ ] O DbgView está pronto para capturar mensagens e há um plano para
      recuperar/reverter a VM após BSOD.

## Limpeza e recuperação

Depois do teste, execute `verifier /reset`, reinicie a VM, desative o test
signing com `bcdedit /set testsigning off` e reinicie novamente. Restaure o
estado normal da blocklist e do HVCI quando a VM voltar a ser usada para outra
finalidade. Em caso de dúvida, descarte a VM e restaure uma nova cópia a partir
da imagem limpa.

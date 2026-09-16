# Teste Seguro do Driver

Este guia descreve um procedimento de teste isolado para o
`ReadWriteDriver`. Use-o somente em máquinas, contas, jogos e ambientes para
os quais você tenha autorização. O procedimento não é apropriado para o host
principal nem para uma máquina que contenha dados de trabalho.

## Por que não testar no host principal

Um driver de kernel com offsets incorretos, hook incompatível ou erro de
inicialização pode causar **BSOD**, perda de estado e reinicializações. Além
disso, o Windows pode impedir o carregamento por causa do **HVCI/Memory
Integrity**, do Secure Boot ou da Vulnerable Driver Blocklist. Desabilitar essas
proteções reduz a segurança da máquina; por isso, faça os testes em uma VM
descartável, com snapshot e sem credenciais ou arquivos importantes.

## Preparar uma VM no Hyper-V

1. Crie uma VM Generation 2 no Hyper-V com uma instalação limpa do Windows 10
   1909 ou 21H1. Reserve CPU e memória suficientes para o sistema e mantenha a
   rede desnecessária desconectada.
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

## Fluxo de teste

1. Desligue a VM e crie um checkpoint imediatamente antes do carregamento.
   Nunca teste sobre o checkpoint limpo; mantenha um ponto para o qual seja
   possível retornar.
2. Copie para a VM, por um meio controlado, os artefatos correspondentes ao
   mesmo build: `ReadWriteDriverMapper.sys` e `kdmapper.exe`. Mantenha também
   o `ReadWriteDriver.sys` disponível se o mapper interno o exigir.
3. Abra um terminal como administrador no diretório dos artefatos e execute:

   ```text
   kdmapper.exe ReadWriteDriverMapper.sys
   ```

   O comando faz manual mapping; não instale o driver como serviço e não
   execute o mapper no host principal. Se o fluxo do build fornecer um
   `ReadWriteDriver.sys` diretamente para o kdmapper, use esse artefato apenas
   conforme a documentação correspondente ao build, pois o mapper interno e o
   utilitário externo não são equivalentes.
4. Abra o **DbgView** como administrador antes ou logo após o comando e
   observe as mensagens do driver. Registre a saída, a versão do Windows, os
   hashes dos binários e qualquer erro de inicialização.
5. Se ocorrer BSOD, travamento, reinicialização ou comportamento inesperado,
   não repita o carregamento automaticamente: desligue a VM e reverta para o
   checkpoint anterior. Preserve os dumps para diagnóstico.
6. Ao terminar, desative o Driver Verifier, reverta as alterações de
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
- [ ] A versão/build do Windows foi verificada e é compatível com os offsets e
      com o driver.
- [ ] Secure Boot e Memory Integrity/HVCI estão desativados somente na VM.
- [ ] `VulnerableDriverBlocklistEnable` está em `0` somente na VM de teste.
- [ ] `bcdedit /set testsigning on` foi aplicado e a VM foi reiniciada.
- [ ] O Driver Verifier está configurado apenas para os drivers em teste.
- [ ] O antivírus/EDR da VM foi desativado temporariamente, somente conforme a
      política do ambiente, e será reativado ao terminar.
- [ ] O build foi produzido com os dados de `PDB_OFFSETS` apropriados para o
      build do Windows; não reutilize offsets de outra versão.
- [ ] `ReadWriteDriverMapper.sys`, o driver necessário e `kdmapper.exe` foram
      obtidos do build esperado e tiveram seus hashes conferidos.
- [ ] O DbgView está pronto para capturar mensagens e há um plano para
      recuperar/reverter a VM após BSOD.

## Limpeza e recuperação

Depois do teste, execute `verifier /reset`, reinicie a VM, desative o test
signing com `bcdedit /set testsigning off` e reinicie novamente. Restaure o
estado normal da blocklist e do HVCI quando a VM voltar a ser usada para outra
finalidade. Em caso de dúvida, descarte a VM e restaure uma nova cópia a partir
da imagem limpa.

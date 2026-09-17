# Toolkit offline de perfis de compatibilidade

`extract_profile.py` lê somente os bytes dos binários indicados. Ele não
carrega driver, não abre dispositivo e não altera `C:\Windows`. O único acesso
externo opcional é ao Microsoft Symbol Server para obter PDBs; o cache fica no
diretório temporário do usuário.

## Uso

Na máquina que será perfilada, com Python 3.12 (ou posterior):

```powershell
py -m pip install pefile capstone requests pdbparse
py tools/profiles/extract_profile.py --system-dir C:\Windows\System32
```

Também é possível informar arquivos explicitamente:

```powershell
py tools/profiles/extract_profile.py `
  --ntoskrnl C:\Windows\System32\ntoskrnl.exe `
  --win32kbase C:\Windows\System32\win32kbase.sys
```

Os artefatos são `generated/<profile_id>.json` e
`generated/profiles_generated.h`. Cada módulo tem identidade própria
(versão, timestamp, imagem, checksum e CodeView GUID+Age); não use o build do
SO para identificar `ntoskrnl` e `win32kbase`.

O `dbghelp.dll` do Windows é tentado primeiro. Se não resolver símbolos, o
programa registra o erro real do symbol server e tenta o caminho de PDB. A
referência RIP-relative de `NtUserSetSysColors` só é aceita quando há um único
candidato em seção de dados. Entradas de IAT em `.idata` também são resolvidas
como evidência, mas não são automaticamente tratadas como slot gravável.
Valores sem validação aparecem como `UNKNOWN` e
não são marcados como válidos no header.

**Cada máquina e cada atualização do Windows exigem um novo perfil.** Este
toolkit não é suporte genérico para builds não perfiladas. O header deve ser
consumido pela lane seguinte somente após revisar o JSON e a tabela de
validação.

@echo off
setlocal
title PubgExt - carregar driver via kdmapper
REM ================================================================
REM  PubgExt - carrega ReadWriteDriverMapper.sys via kdmapper
REM
REM  REGRAS:
REM   1. Rodar SOMENTE em VM com snapshot.
REM   2. Executar como Administrador.
REM   3. Kernel alvo: ntoskrnl 10.0.26100.9457 (perfil embutido).
REM      Outro kernel: o mapper recusa de forma limpa.
REM   4. NUNCA descarregue o driver depois de carregar.
REM
REM  Este .bat:
REM   - COPIA o mapper canonico (ReadWriteDriver\x64\Release) para a pasta
REM     do kdmapper, para nunca testarmos um .sys copiado a mao/desatualizado;
REM   - mostra tamanho e SHA-256 do binario que sera mapeado;
REM   - imprime o ULTIMO BUGCHECK decodificado (nao precisa de DebugView);
REM   - roda o kdmapper gravando a saida em load_driver_kdmapper.last.log;
REM   - verifica o device \\.\PubgExtRw e PAUSA no final.
REM ================================================================

set "RESULT=1"
set "TOOLS=%~dp0"
set "KDMAPPER=%TOOLS%kdmapper-src\x64\Release\kdmapper_Release.exe"
set "CANON=%TOOLS%..\ReadWriteDriver\x64\Release\ReadWriteDriverMapper.sys"
set "TARGET=%TOOLS%kdmapper-src\x64\Release\ReadWriteDriverMapper.sys"
set "RUNLOG=%TOOLS%load_driver_kdmapper.last.log"
set "BUGPS=%TOOLS%last_bugcheck.ps1"
set "DEVPS=%TOOLS%check_device.ps1"

REM ---- 0. precisa ser admin ----
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo [ERRO] Execute este .bat como Administrador.
  goto :pause_end
)

REM ---- 1. ferramentas presentes? ----
if not exist "%KDMAPPER%" (
  echo [ERRO] kdmapper nao encontrado:
  echo        %KDMAPPER%
  echo Compile com: MSBuild tools\kdmapper-src\kdmapper.sln /p:Configuration=Release /p:Platform=x64
  goto :pause_end
)
if not exist "%CANON%" (
  echo [ERRO] mapper canonico nao encontrado:
  echo        %CANON%
  echo Compile a solution do driver e regenere o payload embutido.
  goto :pause_end
)
if not exist "%BUGPS%" echo [AVISO] faltando %BUGPS% (diagnostico de bugcheck sera pulado)
if not exist "%DEVPS%" echo [AVISO] faltando %DEVPS% (checagem do device sera pulada)

REM ---- 2. copiar o mapper CANONICO para a pasta do kdmapper ----
echo [+] Sincronizando o mapper canonico para a pasta do kdmapper...
copy /Y "%CANON%" "%TARGET%" >nul
if errorlevel 1 (
  echo [ERRO] falha ao copiar o mapper para %TARGET%
  goto :pause_end
)
for %%F in ("%TARGET%") do echo     %%~nxF  %%~zF bytes
echo     SHA-256:
powershell -NoProfile -Command "(Get-FileHash -LiteralPath '%TARGET%' -Algorithm SHA256).Hash"
echo.

REM ---- 3. contexto: kernel + ultimo bugcheck (evita depender de DebugView) ----
echo ================================================================
echo  Kernel desta maquina:
powershell -NoProfile -Command "[System.Environment]::OSVersion.Version"
echo ================================================================
echo  ULTIMO BUGCHECK registrado (se o run anterior caiu, os parametros
echo  abaixo dizem o que aconteceu - arg3: 0=LEITURA 1=ESCRITA, arg4=endereco):
if exist "%BUGPS%" powershell -NoProfile -ExecutionPolicy Bypass -File "%BUGPS%"
echo ================================================================
echo  ATENCAO: VM com snapshot. Kernel esperado: ntoskrnl 10.0.26100.9457.
echo ================================================================
echo.

REM ---- 4. mapear com kdmapper (saida para a tela E para o .log) ----
echo [+] Mapeando ReadWriteDriverMapper.sys ...
echo     "%KDMAPPER%" "%TARGET%"
echo     log: %RUNLOG%
"%KDMAPPER%" "%TARGET%" > "%RUNLOG%" 2>&1
set "KD_EXIT=%ERRORLEVEL%"
type "%RUNLOG%"
echo kdmapper exit code: %KD_EXIT%
echo.

REM ---- 5. verificar o device ----
echo [+] Verificando device \\.\PubgExtRw ...
set "DEV_EXIT=1"
if exist "%DEVPS%" powershell -NoProfile -ExecutionPolicy Bypass -File "%DEVPS%"
set "DEV_EXIT=%ERRORLEVEL%"
echo.

REM ---- 6. veredito ----
echo ================================================================
if "%KD_EXIT%"=="0" (
  if "%DEV_EXIT%"=="0" (
    echo [OK] kdmapper OK e device presente. Driver CARREGADO.
    echo Proximo passo: ReadWriteUser.exe como admin para o smoke test.
    set "RESULT=0"
  ) else (
    echo [FALHA] kdmapper disse OK mas o device nao apareceu.
    echo        Veja o payload_status nos traces ^(se tiver capturado^).
  )
) else (
  echo [FALHA] kdmapper falhou ^(exit %KD_EXIT%^). Driver NAO carregado.
  echo        A saida completa esta em: %RUNLOG%
)
echo ================================================================

:pause_end
echo.
pause
exit /b %RESULT%

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
REM      Outro kernel = o mapper deve recusar; se der BSOD, anote o bugcheck.
REM   4. NUNCA descarregue o driver depois de carregar.
REM ================================================================

set "RESULT=1"

REM ---- 0. precisa ser admin ----
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo [ERRO] Execute este .bat como Administrador.
  goto :pause_end
)

set "TOOLS_DIR=%~dp0"
set "KDMAPPER=%TOOLS_DIR%kdmapper-src\x64\Release\kdmapper_Release.exe"
set "DRIVER=%TOOLS_DIR%..\ReadWriteDriver\x64\Release\ReadWriteDriverMapper.sys"

REM ---- 1. arquivos existem? ----
if not exist "%KDMAPPER%" (
  echo [ERRO] kdmapper nao encontrado:
  echo        %KDMAPPER%
  echo Compile com: MSBuild tools\kdmapper-src\kdmapper.sln /p:Configuration=Release /p:Platform=x64
  goto :pause_end
)
if not exist "%DRIVER%" (
  echo [ERRO] driver nao encontrado:
  echo        %DRIVER%
  echo Compile a solution do driver e regenere o payload embutido.
  goto :pause_end
)
for %%F in ("%DRIVER%") do echo [+] driver: %%~nxF ^(%%~zF bytes^)

echo ================================================================
echo  ATENCAO: VM com snapshot. Kernel esperado: ntoskrnl 10.0.26100.9457.
echo  Se der tela azul, fotografe o BUGCHECK CODE antes de reverter.
echo ================================================================
echo Kernel desta maquina:
powershell -NoProfile -Command "[System.Environment]::OSVersion.Version"
echo.

REM ---- 2. mapear com kdmapper (saida vai para a tela E para .log, que sobrevive a reboot) ----
set "RUNLOG=%TOOLS_DIR%load_driver_kdmapper.last.log"
echo [+] Mapeando ReadWriteDriverMapper.sys ...
echo     "%KDMAPPER%" "%DRIVER%"
echo     log: %RUNLOG%
"%KDMAPPER%" "%DRIVER%" > "%RUNLOG%" 2>&1
set "KD_EXIT=%ERRORLEVEL%"
type "%RUNLOG%"
echo kdmapper exit code: %KD_EXIT%
if not "%KD_EXIT%"=="0" (
  echo [AVISO] kdmapper retornou erro. Continue para a verificacao do device.
)
echo.

REM ---- 3. verificar se o device existe ----
echo [+] Verificando device \\.\PubgExtRw ...
set "CHECKPS=%TEMP%\pubgext_check_device.ps1"
>  "%CHECKPS%" echo $ErrorActionPreference = 'Stop'
>> "%CHECKPS%" echo try {
>> "%CHECKPS%" echo   $s = New-Object System.IO.FileStream('\\.\PubgExtRw', 'Open', 'Read', 'Read')
>> "%CHECKPS%" echo   $s.Close()
>> "%CHECKPS%" echo   Write-Host '[OK] device ABERTO: driver carregado e acessivel.'
>> "%CHECKPS%" echo   exit 0
>> "%CHECKPS%" echo } catch [System.IO.FileNotFoundException] {
>> "%CHECKPS%" echo   Write-Host '[FALHA] device NAO existe: driver nao carregado.'
>> "%CHECKPS%" echo   exit 1
>> "%CHECKPS%" echo } catch [System.IO.DirectoryNotFoundException] {
>> "%CHECKPS%" echo   Write-Host '[FALHA] device NAO existe: driver nao carregado.'
>> "%CHECKPS%" echo   exit 1
>> "%CHECKPS%" echo } catch {
>> "%CHECKPS%" echo   Write-Host '[OK] device EXISTE, abertura negada pela ACL/allowlist: driver carregado.'
>> "%CHECKPS%" echo   Write-Host ('       detalhe: ' + $_.Exception.GetType().Name)
>> "%CHECKPS%" echo   exit 0
>> "%CHECKPS%" echo }
powershell -NoProfile -ExecutionPolicy Bypass -File "%CHECKPS%"
set "DEV_EXIT=%ERRORLEVEL%"
del "%CHECKPS%" >nul 2>&1
echo.

REM ---- 4. veredito ----
echo ================================================================
if "%KD_EXIT%"=="0" (
  if "%DEV_EXIT%"=="0" (
    echo [OK] kdmapper OK e device presente. Driver CARREGADO.
    echo Proximo passo: ReadWriteUser.exe como admin para smoke test.
    set "RESULT=0"
  ) else (
    echo [FALHA] kdmapper disse OK mas o device nao apareceu.
  )
) else (
  echo [FALHA] kdmapper falhou. Driver NAO carregado.
)
echo ================================================================

:pause_end
echo.
pause
exit /b %RESULT%

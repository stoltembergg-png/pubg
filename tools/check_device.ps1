# Verifica se o device \\.\PubgExtRw existe.
# Distingue de forma INEQUIVOCA pelo codigo Win32 real (via HResult):
#   2 (ERROR_FILE_NOT_FOUND)   -> device NAO existe  (driver nao carregado)  -> exit 1
#   5 (ERROR_ACCESS_DENIED)    -> device EXISTE, abertura negada (ACL/allowlist) -> exit 0
#   0                          -> device existe E abriu                        -> exit 0
#   outros                     -> reporta o codigo bruto                        -> exit 2
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File check_device.ps1
$ErrorActionPreference = 'Stop'

$win32 = 0
try {
    $s = New-Object System.IO.FileStream('\\.\PubgExtRw', 'Open', 'Read', 'Read')
    $s.Close()
    Write-Host '[OK] device ABERTO com sucesso (codigo Win32 0).'
    exit 0
}
catch {
    # HResult carrega o codigo Win32 nos 16 bits baixos (ex.: 0x80070002 -> 2).
    $ex = $_.Exception
    $hr = $ex.HResult
    if ($ex.InnerException) { $hr = $ex.InnerException.HResult }
    $win32 = $hr -band 0xFFFF
    $type = $ex.GetType().Name
    if ($ex.InnerException) { $type = $ex.InnerException.GetType().Name }
}

switch ($win32) {
    0 {
        Write-Host '[OK] device acessivel (codigo Win32 0).'
        exit 0
    }
    2 {
        Write-Host '[FALHA] device NAO existe (Win32 2 = ERROR_FILE_NOT_FOUND): driver NAO carregado.'
        exit 1
    }
    3 {
        Write-Host '[FALHA] caminho nao encontrado (Win32 3 = ERROR_PATH_NOT_FOUND): driver NAO carregado.'
        exit 1
    }
    5 {
        Write-Host '[OK] device EXISTE, abertura negada (Win32 5 = ERROR_ACCESS_DENIED): ACL/allowlist em vigor.'
        Write-Host ('      excecao: ' + $type)
        exit 0
    }
    default {
        Write-Host ('[?] resultado nao conclusivo: Win32 ' + $win32 + ' (0x' + ('{0:X}' -f $win32) + '), excecao ' + $type)
        Write-Host '    Codigo 2 = nao existe; 5 = existe e negado. Outro codigo: investigar.'
        exit 2
    }
}

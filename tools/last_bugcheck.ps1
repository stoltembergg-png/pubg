# Le o ultimo bugcheck registrado (evento 1001) e imprime codigo + parametros decodificados.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File last_bugcheck.ps1
$ErrorActionPreference = 'SilentlyContinue'

$ev = Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 1001 } -MaxEvents 1
if (-not $ev) {
    Write-Host '[i] Nenhum bugcheck registrado (evento 1001 ausente).'
    exit 0
}

Write-Host ('[i] Ultimo bugcheck: ' + $ev.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss'))

$line = ($ev.Message -split "`n" | Where-Object { $_ -match '0x[0-9a-fA-F]{8}' } | Select-Object -First 1)
if (-not $line) { $line = $ev.Message }
Write-Host ('    bruto : ' + $line.Trim())

$hex = [regex]::Matches($line, '0x[0-9a-fA-F]+') | ForEach-Object { $_.Value }
if ($hex.Count -ge 5) {
    Write-Host ('    code  : ' + $hex[0])
    Write-Host ('    arg1  : ' + $hex[1] + '   (codigo da excecao: 0xC0000005 = access violation)')
    Write-Host ('    arg2  : ' + $hex[2] + '   (endereco da instrucao que falhou)')
    Write-Host ('    arg3  : ' + $hex[3] + '   (0 = LEITURA, 1 = ESCRITA)')
    Write-Host ('    arg4  : ' + $hex[4] + '   (ENDERECO acessado: e isto que aponta o bug)')
}

$dump = ($ev.Message -split "`n" | Where-Object { $_ -match 'Minidump|\.dmp' } | Select-Object -First 1)
if ($dump) { Write-Host ('    dump  : ' + $dump.Trim()) }

# Reinicio inesperado recente (confirma que a maquina caiu e voltou)
$ev41 = Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 41 } -MaxEvents 1
if ($ev41) {
    Write-Host ('[i] Reinicio inesperado (evento 41): ' + $ev41.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss'))
}

exit 0

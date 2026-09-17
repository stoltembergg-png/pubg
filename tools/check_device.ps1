# Verifica se o device \\.\PubgExtRw existe e se pode ser aberto.
# Saida: 0 = existe (carregado), 1 = nao existe (nao carregado).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File check_device.ps1
$ErrorActionPreference = 'Stop'

try {
    $s = New-Object System.IO.FileStream('\\.\PubgExtRw', 'Open', 'Read', 'Read')
    $s.Close()
    Write-Host '[OK] device ABERTO: driver carregado e acessivel.'
    exit 0
}
catch [System.IO.FileNotFoundException] {
    Write-Host '[FALHA] device NAO existe: driver nao carregado.'
    exit 1
}
catch [System.IO.DirectoryNotFoundException] {
    Write-Host '[FALHA] device NAO existe: driver nao carregado.'
    exit 1
}
catch {
    # Excecao diferente de "nao existe" => o device esta la, mas a abertura foi negada.
    Write-Host '[OK] device EXISTE (abertura negada pela ACL/allowlist): driver carregado.'
    Write-Host ('       detalhe: ' + $_.Exception.GetType().Name)
    exit 0
}

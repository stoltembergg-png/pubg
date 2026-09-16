param(
    [string]$Payload = "",
    [string]$Output = "",
    [string]$Symbol = "hexData"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$driverRoot = Split-Path -Parent $scriptDir
if ([string]::IsNullOrWhiteSpace($Payload)) {
    $Payload = Join-Path $driverRoot "x64\Release\ReadWriteDriver.sys"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $driverRoot "ReadWriteDriverMapper\driver.h"
}

$payloadPath = [IO.Path]::GetFullPath($Payload)
$outputPath = [IO.Path]::GetFullPath($Output)
if (-not [IO.File]::Exists($payloadPath)) {
    throw "Payload not found: $payloadPath"
}

[byte[]]$bytes = [IO.File]::ReadAllBytes($payloadPath)
if ($bytes.Length -eq 0) {
    throw "Payload is empty: $payloadPath"
}

$lines = New-Object 'System.Collections.Generic.List[string]'
[void]$lines.Add("#pragma once")
[void]$lines.Add("")
[void]$lines.Add("unsigned char $Symbol[$($bytes.Length)] = {")
for ($offset = 0; $offset -lt $bytes.Length; $offset += 16) {
    $end = [Math]::Min($offset + 15, $bytes.Length - 1)
    $values = ($bytes[$offset..$end] | ForEach-Object { "0x{0:X2}" -f $_ }) -join ", "
    [void]$lines.Add("    $values,")
}
[void]$lines.Add("};")
[void]$lines.Add("")

$parent = Split-Path -Parent $outputPath
if (-not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
[IO.File]::WriteAllText($outputPath, ($lines -join "`n"), [Text.Encoding]::ASCII)
Write-Output "embedded $($bytes.Length) bytes from $payloadPath into $outputPath"

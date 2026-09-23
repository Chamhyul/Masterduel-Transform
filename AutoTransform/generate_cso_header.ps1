$csoPath = Join-Path $PSScriptRoot "AutoTransform.cso"
$rsPath  = Join-Path $PSScriptRoot "AutoTransform.rs"
$outPath = Join-Path $PSScriptRoot "AutoTransform_CSO.h"

$csoBytes = [System.IO.File]::ReadAllBytes($csoPath)
$rsBytes  = [System.IO.File]::ReadAllBytes($rsPath)

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("#include <cstdint>")
[void]$sb.AppendLine()
[void]$sb.AppendLine("// Embedded DirectX 12 Compute Shader Bytecode")
[void]$sb.AppendLine("static const uint8_t kAutoTransformCSO[] = {")

for ($i = 0; $i -lt $csoBytes.Length; $i++) {
    if (($i % 16) -eq 0) { [void]$sb.Append("    ") }
    [void]$sb.Append(('0x{0:x2}, ' -f $csoBytes[$i]))
    if (($i % 16) -eq 15 -or $i -eq ($csoBytes.Length - 1)) { [void]$sb.AppendLine() }
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine(('static const size_t kAutoTransformCSOSize = {0};' -f $csoBytes.Length))
[void]$sb.AppendLine()
[void]$sb.AppendLine("// Embedded DirectX 12 Root Signature Bytecode")
[void]$sb.AppendLine("static const uint8_t kAutoTransformRS[] = {")

for ($i = 0; $i -lt $rsBytes.Length; $i++) {
    if (($i % 16) -eq 0) { [void]$sb.Append("    ") }
    [void]$sb.Append(('0x{0:x2}, ' -f $rsBytes[$i]))
    if (($i % 16) -eq 15 -or $i -eq ($rsBytes.Length - 1)) { [void]$sb.AppendLine() }
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine(('static const size_t kAutoTransformRSSize = {0};' -f $rsBytes.Length))

[System.IO.File]::WriteAllText($outPath, $sb.ToString())
Write-Host "Generated $outPath successfully. CSO size: $($csoBytes.Length) bytes, RS size: $($rsBytes.Length) bytes."

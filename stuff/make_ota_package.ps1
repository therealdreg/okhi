param(
    [Parameter(Mandatory = $true)][string]$EspImage,
    [Parameter(Mandatory = $true)][string]$RpImage,
    [Parameter(Mandatory = $true)][ValidateSet('USB', 'PS2')][string]$Variant,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = 'Stop'

function Get-OkhiCrc32([byte[]]$Data) {
    $table = @(
        0x00000000L, 0x1db71064L, 0x3b6e20c8L, 0x26d930acL, 0x76dc4190L, 0x6b6b51f4L, 0x4db26158L, 0x5005713cL,
        0xedb88320L, 0xf00f9344L, 0xd6d6a3e8L, 0xcb61b38cL, 0x9b64c2b0L, 0x86d3d2d4L, 0xa00ae278L, 0xbdbdf21cL)

    $crc = [int64]0xffffffffL

    foreach ($byte in $Data) {
        $crc = $crc -bxor [int64]$byte
        $crc = (($crc -shr 4) -band 0x0fffffffL) -bxor $table[[int]($crc -band 0x0f)]
        $crc = (($crc -shr 4) -band 0x0fffffffL) -bxor $table[[int]($crc -band 0x0f)]
    }

    return [uint32](($crc -bxor 0xffffffffL) -band 0xffffffffL)
}

function Assert-OkhiCrc32 {
    $probe = [System.Text.Encoding]::ASCII.GetBytes('123456789')
    $value = Get-OkhiCrc32 $probe

    if ($value -ne 0xCBF43926L) {
        throw ("crc32 self test failed: got {0:x8}, expected cbf43926. The package would be rejected by the device." -f $value)
    }
}

Assert-OkhiCrc32

if (-not (Test-Path -LiteralPath $EspImage)) { throw "ESP image not found: $EspImage" }
if (-not (Test-Path -LiteralPath $RpImage)) { throw "RP image not found: $RpImage" }

$esp = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $EspImage))
$rp = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $RpImage))

if ($esp.Length -lt 8192) { throw "ESP image looks too small: $($esp.Length) bytes" }
if ($esp[0] -ne 0xE9) { throw "ESP image does not start with the 0xE9 image magic" }
if ($rp.Length -lt 1024) { throw "RP image looks too small: $($rp.Length) bytes" }

$espCrc = Get-OkhiCrc32 $esp
$rpCrc = Get-OkhiCrc32 $rp

$tag = [System.Text.Encoding]::ASCII.GetBytes($Variant.PadRight(4, [char]0))

$header = New-Object byte[] 32
[System.Text.Encoding]::ASCII.GetBytes('OKHI').CopyTo($header, 0)
[BitConverter]::GetBytes([uint32]1).CopyTo($header, 4)
$tag.CopyTo($header, 8)
[BitConverter]::GetBytes([uint32]$esp.Length).CopyTo($header, 12)
[BitConverter]::GetBytes([uint32]$espCrc).CopyTo($header, 16)
[BitConverter]::GetBytes([uint32]$rp.Length).CopyTo($header, 20)
[BitConverter]::GetBytes([uint32]$rpCrc).CopyTo($header, 24)

$headerCrc = Get-OkhiCrc32 ([byte[]]$header[0..27])
[BitConverter]::GetBytes([uint32]$headerCrc).CopyTo($header, 28)

if (-not [BitConverter]::IsLittleEndian) { throw 'this packager assumes a little endian host' }

$outputDir = Split-Path -Parent $Output
if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

$stream = [System.IO.File]::Create($Output)
try {
    $stream.Write($header, 0, $header.Length)
    $stream.Write($esp, 0, $esp.Length)
    $stream.Write($rp, 0, $rp.Length)
}
finally {
    $stream.Close()
}

$total = 32 + $esp.Length + $rp.Length

Write-Host ("[OK] {0}  variant {1}  esp {2} bytes crc {3:x8}  rp {4} bytes crc {5:x8}  total {6} bytes" -f `
        $Output, $Variant, $esp.Length, $espCrc, $rp.Length, $rpCrc, $total)

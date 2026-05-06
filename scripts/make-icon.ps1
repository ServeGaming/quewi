# Builds resources/icons/quewi.ico from resources/icons/quewi-1024.png.
# Embeds 16/24/32/48/64/128/256 PNG-compressed entries — modern Windows
# ICO format. Uses only stock .NET so no extra tools are required.
#
# Run from the repo root:  pwsh -File scripts/make-icon.ps1
$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot '..\resources\icons\quewi-1024.png' | Resolve-Path
$dst = Join-Path $PSScriptRoot '..\resources\icons\quewi.ico'

Add-Type -AssemblyName System.Drawing

$sizes = 16, 24, 32, 48, 64, 128, 256
$source = [System.Drawing.Image]::FromFile($src)

# Build PNG byte streams for each size.
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.DrawImage($source, 0, 0, $s, $s)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngs += ,$ms.ToArray()
}
$source.Dispose()

# Assemble ICO container per spec.
$out = [System.IO.File]::Open($dst, 'Create')
$bw  = New-Object System.IO.BinaryWriter $out
$bw.Write([UInt16]0)                # reserved
$bw.Write([UInt16]1)                # type = icon
$bw.Write([UInt16]$sizes.Count)     # image count

# Directory entries are 16 bytes each; image data follows.
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $w = $sizes[$i]; if ($w -ge 256) { $w = 0 }   # 0 means 256 in ICO
    $bw.Write([Byte]$w)             # width
    $bw.Write([Byte]$w)             # height
    $bw.Write([Byte]0)              # palette
    $bw.Write([Byte]0)              # reserved
    $bw.Write([UInt16]1)            # color planes
    $bw.Write([UInt16]32)           # bits per pixel
    $bw.Write([UInt32]$pngs[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($png in $pngs) { $bw.Write($png) }
$bw.Close(); $out.Close()

Write-Host "Wrote $dst ($([math]::Round((Get-Item $dst).Length/1KB,1)) KB, sizes: $($sizes -join ', '))"

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = [System.IO.File]::Create((Join-Path $scriptDir "app.ico"))
$writer = New-Object System.IO.BinaryWriter($out)
$images = @()

foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $rect = [System.Drawing.RectangleF]::new($s * 0.08, $s * 0.08, $s * 0.84, $s * 0.84)
    $radius = $s * 0.20
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($rect.X, $rect.Y, $radius, $radius, 180, 90)
    $path.AddArc($rect.Right - $radius, $rect.Y, $radius, $radius, 270, 90)
    $path.AddArc($rect.Right - $radius, $rect.Bottom - $radius, $radius, $radius, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $radius, $radius, $radius, 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, [System.Drawing.Color]::FromArgb(255, 28, 34, 50), [System.Drawing.Color]::FromArgb(255, 14, 142, 218), 45)
    $g.FillPath($brush, $path)

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(235, 255, 255, 255), [Math]::Max(1, $s / 14))
    $g.DrawLine($pen, $s * 0.28, $s * 0.38, $s * 0.43, $s * 0.50)
    $g.DrawLine($pen, $s * 0.28, $s * 0.62, $s * 0.43, $s * 0.50)
    $g.DrawLine($pen, $s * 0.52, $s * 0.64, $s * 0.74, $s * 0.64)

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,@($s, $ms.ToArray())

    $pen.Dispose()
    $brush.Dispose()
    $path.Dispose()
    $g.Dispose()
    $bmp.Dispose()
}

$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count

foreach ($entry in $images) {
    $s = [int]$entry[0]
    $bytes = [byte[]]$entry[1]
    $writer.Write([byte]$(if ($s -eq 256) { 0 } else { $s }))
    $writer.Write([byte]$(if ($s -eq 256) { 0 } else { $s }))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $bytes.Length
}

foreach ($entry in $images) {
    $writer.Write([byte[]]$entry[1])
}

$writer.Close()
$out.Close()

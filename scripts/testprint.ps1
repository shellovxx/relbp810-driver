# Prints a test page through the Windows spooler (GDI) to the given printer.
param(
    [string]$Printer = 'Canon LBP-810',
    [int]$Pages = 1,
    [switch]$Landscape
)
Add-Type -AssemblyName System.Drawing
$doc = New-Object System.Drawing.Printing.PrintDocument
$doc.PrinterSettings.PrinterName = $Printer
$doc.DocumentName = 'LBP-810 driver test page'
$doc.DefaultPageSettings.Landscape = [bool]$Landscape
$script:page = 0
$doc.add_PrintPage({
    param($sender, $e)
    $script:page++
    $g = $e.Graphics
    $g.PageUnit = [System.Drawing.GraphicsUnit]::Millimeter
    $b = $e.PageBounds
    $w = $b.Width * 25.4 / 100
    $h = $b.Height * 25.4 / 100
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::Black), 0.3
    $g.DrawRectangle($pen, 5, 5, $w - 10, $h - 10)
    $title = New-Object System.Drawing.Font 'Arial', 28, ([System.Drawing.FontStyle]::Bold)
    $font = New-Object System.Drawing.Font 'Times New Roman', 12
    $mono = New-Object System.Drawing.Font 'Consolas', 9
    $g.DrawString('Canon LBP-810 x64', $title, [System.Drawing.Brushes]::Black, 15, 15)
    $g.DrawString(("Тестовая страница {0} из {1} - драйвер через спулер Windows" -f $script:page, $Pages), $font, [System.Drawing.Brushes]::Black, 15, 32)
    $g.DrawString(("Printed {0:yyyy-MM-dd HH:mm:ss} on {1}" -f (Get-Date), $env:COMPUTERNAME), $mono, [System.Drawing.Brushes]::Black, 15, 40)
    # 10 mm ruler
    for ($x = 10; $x -lt $w - 5; $x += 10) { $g.DrawLine($pen, $x, 5, $x, 10) }
    for ($y = 10; $y -lt $h - 5; $y += 10) { $g.DrawLine($pen, 5, $y, 10, $y) }
    # gray ramp
    for ($i = 0; $i -le 10; $i++) {
        $c = [int](255 - $i * 25.5)
        $br = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($c, $c, $c))
        $g.FillRectangle($br, 15 + $i * 16, 55, 15, 20)
        $g.DrawString(("{0}%" -f ($i * 10)), $mono, [System.Drawing.Brushes]::Black, 15 + $i * 16, 76)
    }
    # fine lines and circles
    for ($i = 0; $i -lt 40; $i++) { $g.DrawLine($pen, 15, 90, 15 + $i * 4.5, 160) }
    $g.FillEllipse([System.Drawing.Brushes]::Black, 120, 95, 60, 60)
    $g.DrawEllipse($pen, 110, 85, 80, 80)
    $text = 'Съешь же ещё этих мягких французских булок, да выпей чаю. The quick brown fox jumps over the lazy dog 0123456789'
    $y = 175
    foreach ($sz in 6, 8, 10, 12, 14, 18) {
        $f = New-Object System.Drawing.Font 'Arial', $sz
        $g.DrawString($text, $f, [System.Drawing.Brushes]::Black, (New-Object System.Drawing.RectangleF 15, $y, ($w - 30), 30))
        $y += $sz * 0.9 + 4
    }
    $e.HasMorePages = $script:page -lt $Pages
})
$doc.Print()
Write-Host "Sent $Pages page(s) to $Printer"

# 将文件转换为 UTF-8 with BOM 格式
# 用法: .\scripts\convert_to_utf8_bom.ps1

$ErrorActionPreference = "Stop"

Write-Host "=== 转换为 UTF-8 with BOM ===" -ForegroundColor Green

# 目标目录
$targetDir = "src\Lightroom.Core"

if (-not (Test-Path $targetDir)) {
    Write-Host "错误: 找不到目录 $targetDir" -ForegroundColor Red
    exit 1
}

# 需要转换的文件扩展名
$extensions = @("*.cpp", "*.h", "*.hpp", "*.c", "*.cxx")

$totalFiles = 0
$convertedFiles = 0
$skippedFiles = 0

foreach ($ext in $extensions) {
    $files = Get-ChildItem -Path $targetDir -Filter $ext -Recurse -File
    
    foreach ($file in $files) {
        $totalFiles++
        
        try {
            # 读取文件内容（自动检测编码）
            $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8
            
            # 检查是否已经是 UTF-8 with BOM
            $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
            $hasBOM = ($bytes.Length -ge 3) -and ($bytes[0] -eq 0xEF) -and ($bytes[1] -eq 0xBB) -and ($bytes[2] -eq 0xBF)
            
            if ($hasBOM) {
                Write-Host "跳过 (已有 BOM): $($file.FullName)" -ForegroundColor Yellow
                $skippedFiles++
                continue
            }
            
            # 保存为 UTF-8 with BOM
            $utf8WithBom = New-Object System.Text.UTF8Encoding $true
            [System.IO.File]::WriteAllText($file.FullName, $content, $utf8WithBom)
            
            Write-Host "✓ 已转换: $($file.FullName)" -ForegroundColor Green
            $convertedFiles++
        }
        catch {
            Write-Host "✗ 转换失败: $($file.FullName) - $($_.Exception.Message)" -ForegroundColor Red
        }
    }
}

Write-Host "`n=== 转换完成 ===" -ForegroundColor Green
Write-Host "总文件数: $totalFiles" -ForegroundColor Cyan
Write-Host "已转换: $convertedFiles" -ForegroundColor Green
Write-Host "已跳过: $skippedFiles" -ForegroundColor Yellow


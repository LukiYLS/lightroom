# LibRaw DLL 部署脚本
# 将 libraw.dll 复制到编译输出目录

$ErrorActionPreference = "Stop"

Write-Host "=== LibRaw DLL 部署脚本 ===" -ForegroundColor Green

# 查找 DLL 源文件（多个可能的位置）
$dllSources = @(
    "third_party\libraw\buildfiles\release-x86_64\libraw.dll",
    "libraw\buildfiles\release-x86_64\libraw.dll"
)

$dllSource = $null
foreach ($path in $dllSources) {
    if (Test-Path $path) {
        $dllSource = $path
        break
    }
}

if (-not $dllSource) {
    Write-Host "错误: 找不到 libraw.dll" -ForegroundColor Red
    Write-Host "请检查以下路径:" -ForegroundColor Yellow
    foreach ($path in $dllSources) {
        Write-Host "  - $path" -ForegroundColor Yellow
    }
    exit 1
}

Write-Host "源文件: $dllSource" -ForegroundColor Cyan

# 查找输出目录
$outputDirs = @(
    "bin\Release",
    "bin\Debug",
    "src\Lightroom.App\bin\Release",
    "src\Lightroom.App\bin\Debug"
)

$copied = $false
foreach ($dir in $outputDirs) {
    if (Test-Path $dir) {
        $targetPath = Join-Path $dir "libraw.dll"
        Copy-Item $dllSource $targetPath -Force
        Write-Host "✓ 已复制到: $targetPath" -ForegroundColor Green
        $copied = $true
    }
}

if (-not $copied) {
    Write-Host "警告: 未找到输出目录，请手动复制 DLL" -ForegroundColor Yellow
    Write-Host "源文件: $dllSource" -ForegroundColor Cyan
    Write-Host "目标: bin\Release\libraw.dll 或 bin\Debug\libraw.dll" -ForegroundColor Cyan
} else {
    Write-Host "`n=== 部署完成 ===" -ForegroundColor Green
}


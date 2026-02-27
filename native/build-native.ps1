param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $root "bin"
New-Item -ItemType Directory -Path $bin -Force | Out-Null

$src = Join-Path $root "ios_device_bridge.cpp"
$dll = Join-Path $bin "ios_device_bridge.dll"
$obj = Join-Path $bin "ios_device_bridge.obj"

Write-Host "Building native bridge ($Configuration)..."
cl /nologo /std:c++17 /EHsc /LD /DWIN32 /D_WINDOWS /D_USRDLL /D_WINDLL $src /Fe:$dll /Fo:$obj
if ($LASTEXITCODE -ne 0) {
    throw "Native build failed with exit code $LASTEXITCODE."
}

$wpfNativeDir = Join-Path (Split-Path -Parent $root) "wpf\runtimes\win-x64\native"
if (Test-Path (Split-Path -Parent $wpfNativeDir)) {
    New-Item -ItemType Directory -Path $wpfNativeDir -Force | Out-Null
    Copy-Item $dll (Join-Path $wpfNativeDir "ios_device_bridge.dll") -Force
    Write-Host "Copied DLL to WPF runtime folder."
}

Write-Host "Done."

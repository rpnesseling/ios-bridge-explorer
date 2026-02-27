param(
    [string]$SourceDir = "$PSScriptRoot\..\native\third_party\libimobiledevice\bin",
    [switch]$SkipCopy
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$runtimeDir = Join-Path $root "wpf\runtimes\win-x64\native"
$outputDir = Join-Path $root "wpf\bin\Release\net8.0-windows"

$required = @(
    "libimobiledevice-1.0.dll",
    "libplist-2.0.dll",
    "libusbmuxd-2.0.dll"
)

$optional = @(
    "libssl-3-x64.dll",
    "libcrypto-3-x64.dll",
    "zlib1.dll"
)

Write-Host "Runtime dir: $runtimeDir"
Write-Host "Output dir : $outputDir"

New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null

if (-not $SkipCopy) {
    if (-not (Test-Path $SourceDir)) {
        Write-Warning "SourceDir not found: $SourceDir"
    }
    else {
        Write-Host "Copying DLLs from: $SourceDir"
        Copy-Item (Join-Path $SourceDir "*.dll") $runtimeDir -Force -ErrorAction SilentlyContinue

        if (Test-Path $outputDir) {
            Copy-Item (Join-Path $SourceDir "*.dll") $outputDir -Force -ErrorAction SilentlyContinue
        }
    }
}

$missingRequired = @()
foreach ($dll in $required) {
    if (-not (Test-Path (Join-Path $runtimeDir $dll))) {
        $missingRequired += $dll
    }
}

$missingOptional = @()
foreach ($dll in $optional) {
    if (-not (Test-Path (Join-Path $runtimeDir $dll))) {
        $missingOptional += $dll
    }
}

Write-Host ""
Write-Host "Validation (runtime folder):"
foreach ($dll in $required + $optional) {
    $exists = Test-Path (Join-Path $runtimeDir $dll)
    $state = if ($exists) { "OK" } else { "MISSING" }
    Write-Host ("  {0,-8} {1}" -f $state, $dll)
}

if ($missingRequired.Count -gt 0) {
    Write-Error ("Missing required runtime DLLs: " + ($missingRequired -join ", "))
}

if ($missingOptional.Count -gt 0) {
    Write-Warning ("Optional runtime DLLs not found: " + ($missingOptional -join ", "))
}

Write-Host ""
Write-Host "Next step: run the app and click Diagnostics to confirm runtime loading."

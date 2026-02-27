param(
    [string]$Configuration = "Release",
    [string]$SourceDir = "$PSScriptRoot\..\native\third_party\libimobiledevice\bin",
    [switch]$SkipNativeBuild,
    [switch]$SkipRuntimeSetup,
    [switch]$SkipWpfBuild
)

$ErrorActionPreference = "Stop"

function Test-CommandAvailable {
    param([Parameter(Mandatory = $true)][string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Test-RequiredRuntimeDlls {
    param(
        [Parameter(Mandatory = $true)][string]$RuntimeDir,
        [Parameter(Mandatory = $true)][string[]]$RequiredDlls
    )

    $missing = @()
    foreach ($dll in $RequiredDlls) {
        if (-not (Test-Path (Join-Path $RuntimeDir $dll))) {
            $missing += $dll
        }
    }

    return $missing
}

$root = Split-Path -Parent $PSScriptRoot
$runtimeDir = Join-Path $root "wpf\runtimes\win-x64\native"
$requiredRuntimeDlls = @(
    "libimobiledevice-1.0.dll",
    "libplist-2.0.dll",
    "libusbmuxd-2.0.dll"
)

$errors = @()

Write-Host "== iOS Bridge Explorer Bootstrap =="
Write-Host "Root         : $root"
Write-Host "Configuration: $Configuration"
Write-Host ""

if (-not (Test-CommandAvailable -Name "dotnet")) {
    $errors += "Missing .NET SDK: 'dotnet' command not found. Install .NET 8 SDK."
}
elseif (-not $SkipWpfBuild) {
    $dotnetVersion = & dotnet --version
    Write-Host "dotnet       : $dotnetVersion"
}

if (-not $SkipNativeBuild -and -not (Test-CommandAvailable -Name "cl")) {
    $errors += "Missing MSVC compiler: 'cl' command not found. Run this script from Developer PowerShell for Visual Studio 2022 with C++ tools."
}

if ($errors.Count -gt 0) {
    Write-Host ""
    Write-Host "Bootstrap precheck failed:"
    foreach ($err in $errors) {
        Write-Host "  - $err"
    }
    throw "Cannot continue until prerequisite issues are fixed."
}

if (-not $SkipNativeBuild) {
    Write-Host ""
    Write-Host "Step 1/3: Build native bridge"
    Push-Location (Join-Path $root "native")
    try {
        .\build-native.ps1 -Configuration $Configuration
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Host ""
    Write-Host "Step 1/3: Build native bridge (skipped)"
}

if (-not $SkipRuntimeSetup) {
    Write-Host ""
    Write-Host "Step 2/3: Setup user-supplied runtime DLLs"
    if (Test-Path $SourceDir) {
        & (Join-Path $PSScriptRoot "setup-libimobiledevice-runtime.ps1") -SourceDir $SourceDir
    }
    else {
        Write-Warning "Source runtime folder not found: $SourceDir"
        Write-Warning "Skipping copy. Will validate if required runtime DLLs are already present in $runtimeDir"
    }
}
else {
    Write-Host ""
    Write-Host "Step 2/3: Setup user-supplied runtime DLLs (skipped)"
}

$missingRuntime = Test-RequiredRuntimeDlls -RuntimeDir $runtimeDir -RequiredDlls $requiredRuntimeDlls
if ($missingRuntime.Count -gt 0) {
    Write-Host ""
    Write-Host "Runtime validation failed. Missing required DLLs:"
    foreach ($dll in $missingRuntime) {
        Write-Host "  - $dll"
    }
    Write-Host ""
    Write-Host "Fix:"
    Write-Host "  1) Put required DLLs in: $SourceDir"
    Write-Host "  2) Re-run: .\scripts\bootstrap.ps1"
    throw "Missing required runtime DLLs."
}

if (-not $SkipWpfBuild) {
    Write-Host ""
    Write-Host "Step 3/3: Build WPF app"
    Push-Location (Join-Path $root "wpf")
    try {
        & dotnet build -c $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Host ""
    Write-Host "Step 3/3: Build WPF app (skipped)"
}

Write-Host ""
Write-Host "Bootstrap complete."
Write-Host "Run the app with:"
Write-Host "  cd wpf"
Write-Host "  dotnet run -c $Configuration"

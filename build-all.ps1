$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

Push-Location (Join-Path $root "native")
try {
    .\build-native.ps1
}
finally {
    Pop-Location
}

Push-Location (Join-Path $root "wpf")
try {
    dotnet build -c Release
}
finally {
    Pop-Location
}

Write-Host "Build complete."

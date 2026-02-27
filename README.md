# iOS Bridge Explorer

This repository contains a two-layer architecture for an iOS file explorer:

- `native/` - C/C++ device bridge DLL (`ios_device_bridge.dll`)
- `wpf/` - C# WPF UI (`IOSBridgeExplorer.UI`)

The native layer provides:
- Device enumeration
- Device open/close
- Directory listing
- Pull/Push file operations (AFC)

The implementation uses `libimobiledevice` at runtime via dynamic loading (`libimobiledevice-1.0.dll`).

## Third-Party Runtime Policy

This repository does **not** redistribute third-party runtime binaries.

- Runtime DLLs are user-supplied at setup time.
- Local staging path: `native\third_party\libimobiledevice\bin\`
- Local runtime path used by the app: `wpf\runtimes\win-x64\native\`

For public/source distribution, keep third-party DLLs out of version control.
See `THIRD_PARTY_NOTICES.md` for additional legal and attribution notes.

## Project Layout

- `native/ios_device_bridge.h` - exported C API
- `native/ios_device_bridge.cpp` - `libimobiledevice` AFC-backed implementation
- `native/build-native.ps1` - build script for native DLL (MSVC)
- `wpf/IOSBridgeExplorer.UI.csproj` - WPF app
- `wpf/*` - UI + MVVM + P/Invoke wrapper

## Prerequisites

- Windows
- Visual Studio 2022 (Desktop development with C++)
- .NET 8 SDK (for WPF build) or Visual Studio with .NET desktop workload
- User-provided `libimobiledevice` runtime DLLs:
  - `libimobiledevice-1.0.dll` (required)
  - plus its dependencies (for example `libplist-2.0.dll`, `libusbmuxd-2.0.dll`, OpenSSL runtime DLLs, etc.)
  - copied into `wpf\runtimes\win-x64\native\` via setup script

## Build Native DLL

Use Developer PowerShell for VS:

```powershell
cd native
.\build-native.ps1
```

Output:

- `native\bin\ios_device_bridge.dll`

## Bootstrap (Recommended)

Run a single setup/build flow with clear prerequisite checks:

```powershell
.\scripts\bootstrap.ps1
```

What it does:

- checks for required tooling (`dotnet`, and `cl` unless native build is skipped)
- builds native bridge
- copies/validates user-supplied `libimobiledevice` runtime DLLs
- builds the WPF app

Optional switches:

- `-SkipNativeBuild`
- `-SkipRuntimeSetup`
- `-SkipWpfBuild`
- `-SourceDir <path-to-user-supplied-dlls>`

## Build and Run WPF App

```powershell
cd wpf
dotnet build
dotnet run
```

The app expects the DLL at:

- `wpf\runtimes\win-x64\native\ios_device_bridge.dll`

`build-native.ps1` copies it there automatically when the folder exists.

## Setup libimobiledevice Runtime (Windows)

Place runtime DLLs in:

- `native\third_party\libimobiledevice\bin\`

Then run:

```powershell
.\scripts\setup-libimobiledevice-runtime.ps1
```

This copies DLLs to `wpf\runtimes\win-x64\native\` (and current `wpf\bin\Release\...` if present) and validates required files.

## Runtime Notes

- If `Refresh Devices` fails with a native error about `libimobiledevice` not found, install/copy the runtime DLLs and restart the app.
- On first connect, unlock the iPhone/iPad and tap `Trust` for this PC.
- AFC typically exposes media/file-sharing areas, not full root filesystem access on non-jailbroken devices.
- Use the new `Diagnostics` button in the app toolbar for a detailed dependency report.

## Notes

- The UI is intentionally MVVM and service-driven to keep native interop isolated.
- Errors from native are surfaced through `iosb_get_last_error()`.
- iOS, iPhone, and Apple are trademarks of Apple Inc. This project is an independent tool and is not affiliated with Apple.


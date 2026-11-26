> [!NOTE]
> Consolidated into the unified Holo v11 monorepo at https://github.com/awesome-os/awesome-os (all existing v11 CAS release assets remain downloadable).

# `@awesome-os/dotnet` - Zero-Installer Native C11 .NET SDK Launcher

Zero-dependency native C11 `.NET SDK` launcher (~25 KB, zero shell shims, zero 1 GB installer bloat) that hydrates rolling-latest .NET SDKs **once** from [`awesome-os/b8g-ghost`](https://github.com/awesome-os/b8g-ghost) (`cas-v1`, Keep-Last-5) into `~/.b8g/cas/dotnet/<sha256>/`.

## Quick Install & Usage

### Windows (PowerShell 1-liner)

```powershell
irm https://github.com/awesome-os/dotnet/releases/latest/download/install.ps1 | iex
```

### Linux (x64 / ARM64) & macOS (Apple Silicon)

```sh
curl -fsSL https://github.com/awesome-os/dotnet/releases/latest/download/install.sh | sh
```

### CLI Usage

```powershell
# Inspect installed SDK / launcher version & help
dotnet --version
dotnet --help

# Build or run a .NET project
dotnet build
dotnet run --project ./MyApp

# Optional: store hydrated CAS layers on a custom drive/path
$env:B8G_GHOST_CACHE_DIR = "D:\b8g-cas"   # POSIX: export B8G_GHOST_CACHE_DIR=/data/b8g-cas
dotnet --ghost-info
```

### Releases & Verification

- **Latest Release:** [`awesome-os/dotnet` Releases](https://github.com/awesome-os/dotnet/releases/latest) — includes `SHA256SUMS.txt` and the 5-OS matrix (`win64`, `linux-x64`, `linux-arm64`, `macos-arm64`, `wasm32-wasi`) built via [`awesome-os/maintenance-workflows`](https://github.com/awesome-os/maintenance-workflows).

## Key Features

- **Zero Installer Bloat:** Tiny native binary (~25 KB) replaces multi-hundred-MB MSI/pkg installers.
- **Zero Telemetry by Default:** Automatically sets `DOTNET_CLI_TELEMETRY_OPTOUT=1` and `DOTNET_ROOT=<cas>/dotnet/<sha256>` before launching.
- **Verbatim Win32 Execution:** On Windows (`_WIN32`), forwards arguments directly via `GetCommandLineW()` + `CreateProcessW()` with zero `cmd.exe` mangling.
- **5-Target Cross-Compilation:** Ships static binaries for Windows x64, Linux x64 (musl), Linux arm64 (musl), macOS arm64, and `wasm32-wasi` (for `--ghost-version`, `--help`, and CAS manifest inspection).

## Supported Targets

| Target | Artifact | CAS Key |
| :--- | :--- | :--- |
| Windows x64 | `dist/dotnet.exe` | `dotnet-sdk-win64.tar.zst` |
| Linux x64 (static musl) | `dist/dotnet-linux-x64` | `dotnet-sdk-linux-x64.tar.zst` |
| Linux arm64 (static musl) | `dist/dotnet-linux-arm64` | `dotnet-sdk-linux-arm64.tar.zst` |
| macOS arm64 | `dist/dotnet-macos-arm64` | `dotnet-sdk-macos-arm64.tar.zst` |
| WASI (`wasm32-wasi`) | `dist/dotnet.wasm` | Manifest & version inspection |

## Usage

```powershell
# Inspect launcher version & CAS target
dotnet --ghost-version

# Inspect cached CAS manifest entry
dotnet --ghost-info

# Standard .NET SDK commands (hydrates once on first run)
dotnet --info
dotnet build
dotnet test
```

## Build (`holo-compiler` / `zig cc` 0.14.1)

```powershell
# Win64 PE
zig cc -target x86_64-windows-gnu -std=c11 -O2 -s -Wall -Wextra src/dotnet_launcher.c -o dist/dotnet.exe

# Linux x64 ELF64 (static musl)
zig cc -target x86_64-linux-musl -static -std=c11 -O2 -s -Wall -Wextra src/dotnet_launcher.c -o dist/dotnet-linux-x64

# Linux arm64 ELF64 (static musl)
zig cc -target aarch64-linux-musl -static -std=c11 -O2 -s -Wall -Wextra src/dotnet_launcher.c -o dist/dotnet-linux-arm64

# macOS arm64 Mach-O
zig cc -target aarch64-macos -std=c11 -O2 -Wall -Wextra src/dotnet_launcher.c -o dist/dotnet-macos-arm64

# WASI (wasm32-wasi)
zig cc -target wasm32-wasi -std=c11 -Os -s -Wall -Wextra src/dotnet_launcher.c -o dist/dotnet.wasm
```

## License

MIT

## Quick Install & Run (Windows / Linux / macOS)

```sh
# Install / hydrate from v11 CAS via holo-pm
holo-pm install repository://github.com/awesome-os/dotnet@main

# Or project & run directly via holo-project (0-byte hardlink)
holo-project "project://node/dotnet?from=repository://github.com/awesome-os/dotnet@main"
```

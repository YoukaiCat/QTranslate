# qtranslate

[![build](https://github.com/youkaicat/qtranslate/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/youkaicat/qtranslate/actions/workflows/build.yml)

Fast and simple tray translator for Windows.

> **Status:** 0.9.0 release candidate. Windows installer and portable ZIP are available.
> Linux support is in development.

**English** | [Русский](README.ru.md)

## Screenshots

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="Main translator window" width="620">
</p>

<p align="center">
  <img src="docs/screenshots/popup-translation.png" alt="Popup translation with Ctrl+Q" width="460">
</p>

<p align="center">
  <img src="docs/screenshots/settings-translation.png" alt="Translation settings" width="48%">
  <img src="docs/screenshots/settings-hotkeys.png" alt="Hotkey settings" width="48%">
</p>

## Main features

- **Ctrl+Q** translates the selected text in a compact window next to the cursor.
- **Ctrl+Shift+Q** translates the selected text and replaces the selection in the active input
  field, which is convenient for replying in chats.
- **Ctrl+W** opens the main window with the selection translated.
- Google Translate (free endpoint or Cloud API) and DeepL (Free/Pro).
- Automatic source language detection, smart switching to the additional language, in-memory
  translation cache and automatic translation while typing.
- System/manual HTTP and SOCKS5 proxies with optional authentication.
- English and Russian interface, autostart, portable INI mode, `--verbose` logging.

## Download

- [Windows installer](https://github.com/youkaicat/qtranslate/releases/download/v0.9.0/qtranslate-0.9.0-win64-setup.exe)
  — per-user installation without administrator rights, Start menu shortcut and uninstaller.
- [Portable ZIP](https://github.com/youkaicat/qtranslate/releases/download/v0.9.0/qtranslate-0.9.0-win64.zip)
  — extract and run `bin\qtranslate.exe`; no installation required.
- [SHA-256 checksums](https://github.com/youkaicat/qtranslate/releases/download/v0.9.0/SHA256SUMS.txt)

Settings and hotkeys are in the tray menu. For portable mode, put an empty `qtranslate.ini` next to
`qtranslate.exe`.

## Build

### Windows

Requires Windows 10/11 x64, Qt 6.5 or newer, CMake 3.19 or newer and Visual Studio 2022.

```bat
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

To build the portable ZIP and installer, install [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php)
and run:

```powershell
./packaging/windows/package.ps1 -BuildDir build/release
```

The output goes to `build/artifacts`. The CI workflow
[`.github/workflows/build.yml`](.github/workflows/build.yml) runs the same steps on
`windows-latest` with Qt 6.8.3 LTS.

### Linux (in progress)

The Linux port is not finished yet. The core, network layer and Qt Widgets UI are designed to be
portable, but the platform modules are currently stubs: global hotkeys, selection capture, paste
into the active window and autostart are not implemented.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
```

The text being translated is sent to the selected online service (Google or DeepL). qtranslate
does not write the texts to its log.

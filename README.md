# qtranslate

Fast and simple desktop translator for Windows. It lives in the system tray and translates
the text selected in any application with a global hotkey.

> **Status:** early development. The application skeleton (tray icon, main window,
> single instance) works; translation is not implemented yet. See the [plan](doc/plan.md).

## Features (planned)

- Starts minimized to the system tray, optional autostart with Windows.
- **Ctrl+Q** translates the selected text in a popup next to the cursor.
- **Ctrl+W** translates the selected text in the main window.
- Hotkeys are configurable.
- Translation services: Google Translate (free endpoint or Cloud API key) and DeepL (API key).
- HTTP and SOCKS5 proxy support, with or without authentication, or the system proxy.

## Building

Requirements:

- Windows 10/11
- Qt 6.5 or newer (developed with Qt 6.12) with the MSVC 2022 x64 kit
- CMake 3.19 or newer
- Visual Studio 2022 (MSVC)

Open `CMakeLists.txt` in Qt Creator, or build from a *x64 Native Tools Command Prompt*:

```bat
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

With CMake older than 3.22, the Qt `bin` directory (e.g. `C:\Qt\6.12.0\msvc2022_64\bin`) must be
in `PATH` for `ctest` to find the Qt DLLs.

Unit tests are built by default; pass `-DQTRANSLATE_BUILD_TESTS=OFF` to skip them.

To produce a self-contained folder with the Qt libraries:

```bat
cmake --install build/release --prefix dist
```

## Usage

```
qtranslate [--minimized]
```

- `--minimized` starts hidden in the tray (used by autostart).
- Starting a second copy brings the window of the running one to the front.
- Closing the window hides it to the tray; use **Quit** in the tray menu to exit.

Verbose logging: set `QT_LOGGING_RULES=qtranslate.*=true`.

## Documentation

Design documents are in Russian:

- [doc/architecture.md](doc/architecture.md) — architecture
- [doc/plan.md](doc/plan.md) — implementation plan

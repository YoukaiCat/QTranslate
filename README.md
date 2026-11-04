# qtranslate

Fast and simple desktop translator for Windows. It lives in the system tray and translates
the text selected in any application with a global hotkey.

> **Status:** early development. The tray icon, single instance, translation in the main
> window through Google Translate (free endpoint), the **Ctrl+W** scenario (translate the
> selected text in the main window) and the **Ctrl+Q** popup next to the cursor work;
> settings and global hotkey configuration are not implemented yet. See the
> [plan](doc/plan.md).

## Features

Working now:

- Lives in the system tray (`--minimized` starts hidden), one instance per user.
- Main window translation with automatic source language detection; long texts are split
  into parts and translated completely.
- **Ctrl+W** translates the text selected in any application in the main window. The
  clipboard contents (text, image or files) are restored after the selection is copied.
- **Ctrl+Q** translates the selected text in a compact window next to the cursor. The window
  has a loading indicator, error and retry states, a target-language menu, Copy, “Open in
  Main Window”, and a pin that keeps it open when focus is lost.

Planned:

- Optional autostart with Windows.
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
- Select text in any application and press **Ctrl+W**: the main window opens with the
  selection translated. The clipboard contents are restored afterwards. Without a selection
  it just opens the main window with the focus in the source field.
- Select text and press **Ctrl+Q**: a translation appears next to the cursor. Click
  “Auto → …” to change the target language and translate again, use **Copy**, or send the
  source and the translation to the main window. **Esc**, the ✕ button or a click outside
  closes the window; 📌 keeps it open when focus is lost. The selection is copied through the
  clipboard and the previous clipboard contents are restored.
- In the main window, type or paste text and press **Translate** or **Ctrl+Enter**. The source
  language defaults to auto-detection, the target language to the system language. **⇄** swaps
  the languages and translates the translation back; **Copy** puts the translation on the
  clipboard. The detected language and errors are shown in the status bar.

The text you translate is sent to the selected online service (currently Google Translate).
The application does not log the texts.

Verbose logging: set `QT_LOGGING_RULES=qtranslate.*=true`.

## Documentation

Design documents are in Russian:

- [doc/architecture.md](doc/architecture.md) — architecture
- [doc/plan.md](doc/plan.md) — implementation plan

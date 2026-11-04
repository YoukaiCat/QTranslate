# qtranslate

Fast and simple desktop translator for Windows. It lives in the system tray and translates
the text selected in any application with a global hotkey.

> **Status:** early development. The tray icon, single instance, translation in the main
> window through Google Translate (free endpoint or Cloud API), **DeepL**, **Ctrl+W**, the
> **Ctrl+Q** popup, settings (including configurable hotkeys and autostart), the portable INI
> mode and proxy support work. Remaining work is polish (caching, auto-translation,
> localization and packaging). See the [plan](doc/plan.md).

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
- Settings dialog: autostart with Windows, start minimized, clipboard restore, popup focus
  behavior, hotkeys for both scenarios, source/target languages, maximum text length and the
  auto-translate delay.
- Settings are stored in `%APPDATA%\qtranslate\qtranslate.ini`; a `qtranslate.ini` next to the
  executable switches to portable mode. API keys and proxy passwords are protected with DPAPI.
- Translation services: Google Translate (free endpoint or Cloud API key) and DeepL (Free/Pro
  API key), switchable in the main window or from the tray “Service” menu. DeepL keys can be
  checked with a button that shows the used character limit.
- Language lists follow the selected service; an unsupported language is replaced with the
  closest one supported by that service.
- Proxy support: no proxy, the Windows system proxy, or a manual HTTP/SOCKS5 proxy with optional
  authentication. Proxy passwords are protected with DPAPI. “Test connection” checks the values
  before saving them.

Planned:

- Translation cache, automatic translation while typing, UI localization and a packaged
  distribution (stage 8).

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
- Open **Settings…** from the tray menu to change autostart, hotkey combinations, languages,
  maximum text length, the auto-translate delay, and Google Cloud/DeepL API keys. Global hotkeys
  are disabled while a key combination field is focused. Settings apply immediately and survive a
  restart.
- Select the active translation service in the main window or in the tray **Service** menu. Without
  a DeepL key, or with Google Cloud selected but no key, translation shows a clear “needs an API
  key” message.
- The **Network** tab configures no proxy, the system proxy, or a manual HTTP/SOCKS5 proxy with
  optional authentication and a request timeout. “Test connection” checks the current fields
  without saving.
- Portable mode: put an empty `qtranslate.ini` next to `qtranslate.exe` and the application
  will use that file instead of `%APPDATA%\qtranslate\qtranslate.ini`.
- With **Start qtranslate when Windows starts** enabled, the application is launched with
  `--minimized` and starts in the tray.

The text you translate is sent to the selected online service (currently Google Translate).
The application does not log the texts.

Verbose logging: set `QT_LOGGING_RULES=qtranslate.*=true`.

## Documentation

Design documents are in Russian:

- [doc/architecture.md](doc/architecture.md) — architecture
- [doc/plan.md](doc/plan.md) — implementation plan

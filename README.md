# QTranslate

[![build](https://github.com/youkaicat/QTranslate/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/youkaicat/QTranslate/actions/workflows/build.yml)

Lightweight desktop translator. It lives in the system tray, translates the text
selected in any application with a global hotkey, and can replace the selection with the
translation directly in the active input field.

> **Status:** 0.11.0 released. The Windows installer and the portable archive are available.
> See the [0.11.0 release notes](doc/releases/0.11.0.md) for the changes.
> Linux support is in development.

**English** | [Русский](README.ru.md)

## Main features

- **Ctrl+Q** translates the selected text in a compact window next to the cursor.
- **Ctrl+Shift+Q** translates the selected text and replaces the selection in the active input
  field, which is convenient for replying in chats.
- **Ctrl+W** opens the main window with the selection translated.
- Google Translate (free endpoint or Cloud API) and DeepL (API key or unofficial free web mode),
  plus OpenRouter or another OpenAI-compatible provider. OpenRouter offers free models such as
  `openrouter/free`; any compatible endpoint works, including local Ollama, LM Studio or
  llama.cpp. See [Translation providers](#translation-providers) for setup.
- Automatic source language detection, smart switching to the additional language, in-memory
  translation cache and automatic translation while typing.
- System/manual HTTP and SOCKS5 proxies with optional authentication.
- Interface in 15 languages (English, Russian, Spanish, German, French, Portuguese, Italian,
  Polish, Ukrainian, Turkish, Chinese, Japanese, Korean, Arabic, Hindi), autostart and portable
  INI mode.

## Screenshots

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="Main translator window" width="620">
</p>

<p align="center">
  <img src="docs/screenshots/popup-translation.png" alt="Popup translation with Ctrl+Q" width="560">
</p>

<p align="center">
  <img src="docs/screenshots/settings-translation.png" alt="Translation settings" width="49%">
  <img src="docs/screenshots/settings-hotkeys.png" alt="Hotkey settings" width="49%">
</p>

## Download and install

The latest build is on the [Releases](https://github.com/youkaicat/QTranslate/releases/latest) page:

- **[`qtranslate-0.11.0-win64-setup.exe`](https://github.com/youkaicat/QTranslate/releases/download/v0.11.0/qtranslate-0.11.0-win64-setup.exe)**. Installs QTranslate for the current user
  without administrator rights; by default it creates a Start menu shortcut and enables autostart,
  with an optional desktop shortcut. It also adds an uninstaller. Uninstalling keeps the settings
  in `%APPDATA%\qtranslate`.
- **[`qtranslate-0.11.0-win64.zip`](https://github.com/youkaicat/QTranslate/releases/download/v0.11.0/qtranslate-0.11.0-win64.zip)**. Extract it and run `bin\qtranslate.exe`;
  no installation or administrator rights are needed. The archive is self-contained: Qt and the
  MSVC runtime are included.
The installer is not code-signed yet, so Windows SmartScreen may show a warning.

## Usage

```
qtranslate [--minimized] [--verbose]
```

- `Ctrl+Q` — translate the selection in a popup next to the cursor.
- `Ctrl+Shift+Q` — translate the selection and replace it in the active input field.
- `Ctrl+W` — open the main window with the selection translated.
- `Ctrl+Enter` — translate in the main window.
- Settings, service selection, **Show window** and **Quit** are available from the tray menu.
  Hotkeys, languages, proxy, autostart and API keys are configured in **Settings**.
- `--minimized` starts hidden in the tray; `--verbose` writes a detailed log to
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`.
- Portable mode: put an empty `qtranslate.ini` next to `qtranslate.exe` and the application will
  use it instead of `%APPDATA%\qtranslate\qtranslate.ini`.

## Translation providers

- **Google Translate** — free public endpoint or Cloud Translation API with a key. Choose the mode
  in **Settings → Services → Google Translate**.
- **DeepL** — official API with a key or the unofficial free web mode without a key. Both modes are
  in **Settings → Services → DeepL**.
- **OpenAI-compatible provider** — the default server is [OpenRouter](https://openrouter.ai)
  (`https://openrouter.ai/api/v1`). OpenRouter **requires a key**; it offers free models such as
  `openrouter/free` and paid models. Paste the key, keep or change the model, and press
  **Load models** if needed.
  The same server URL field accepts any OpenAI-compatible endpoint: OpenAI
  (`https://api.openai.com/v1`), TeamRouter, LM Studio, llama.cpp, or a local Ollama server. The
  model name must match the model exposed by that server.

### Ollama (local, no key)

1. Install [Ollama](https://ollama.com) for Windows, macOS or Linux.
2. Start it and download a model, for example:
   ```bat
   ollama pull llama3.2
   ```
3. In QTranslate open **Settings → Services → OpenAI-compatible provider** and set:
   - Server URL: `http://localhost:11434/v1`
   - API key: leave empty
   - Model: the name from `ollama list`, for example `llama3.2`
4. Press **Load models** or type the model name, then **OK**. Select **Local LLM** in the service list.

Ollama usually listens on `http://localhost:11434`; if the server is not running, start it with
`ollama serve`. Its OpenAI-compatible API is under `/v1`. More details:
[Ollama OpenAI compatibility](https://docs.ollama.com/api/openai-compatibility).

## Build

### Windows

Requirements:

- Windows 10/11, 64-bit
- Qt 6.5 or newer (developed with Qt 6.12) with the MSVC 2022 x64 kit
- CMake 3.19 or newer
- Visual Studio 2022 (MSVC)

Open `CMakeLists.txt` in Qt Creator, or build from an *x64 Native Tools Command Prompt*:

```bat
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

With CMake older than 3.22, the Qt `bin` directory must be in `PATH` for `ctest` to find the Qt
DLLs. Unit tests are built by default; pass `-DQTRANSLATE_BUILD_TESTS=OFF` to skip them.

To build the release artifacts (portable ZIP and per-user installer), install
[Inno Setup 6.3+](https://jrsoftware.org/isinfo.php) and run:

```powershell
./packaging/windows/package.ps1 -BuildDir build/release
```

The output goes to `build/artifacts`. The CI workflow
[`.github/workflows/build.yml`](.github/workflows/build.yml) runs the same steps on
`windows-latest` with Qt 6.8.3 LTS. Pushing a tag such as `v0.11.0` publishes a GitHub release with
these artifacts; the tag must match the version in `CMakeLists.txt`.

### Linux (in progress)

The Linux port is not finished yet. The core, network layer and Qt Widgets UI are designed to be
portable, but the platform modules are currently stubs: global hotkeys, selection capture, paste
into the active window and autostart are not implemented. A full Linux port is planned for a later
release; CI currently builds Windows only.

To build the current Linux tree for development:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
```

## Known limitations

- Selection capture does not work in windows of processes started as administrator.
- The free Google endpoint can answer `HTTP 429` when it rate-limits a client; wait or use Google
  Cloud/DeepL with an API key.
- DeepL free web mode is an unofficial endpoint without an API key. It may be rate-limited, change
  or stop working; the official DeepL API is more reliable.
- OpenRouter and other OpenAI-compatible providers need an API key and an available model. Quality,
  speed and cost depend on the chosen model; free models have strict rate limits. The text is sent
  to the selected provider.
- The intermediate `Ctrl+C` used to grab a selection may appear in the Windows clipboard history
  (`Win+V`); the original clipboard contents are restored.
- The installer is not code-signed yet.
- The Hindi interface is translated, but Qt 6.12 has no Hindi catalog, so standard Qt dialogs
  remain English.
- Linux support is not ready yet; see the build section above.

The text being translated is sent to the selected provider: Google, DeepL, OpenRouter or another
OpenAI-compatible server. A local server such as Ollama keeps the text on your machine. QTranslate
does not write the texts to its log.

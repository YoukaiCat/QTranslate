# QTranslate

[![build](https://github.com/youkaicat/QTranslate/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/youkaicat/QTranslate/actions/workflows/build.yml)

Lightweight desktop translator. It lives in the system tray, translates the text
selected in any application with a global hotkey, and can replace the selection with the
translation directly in the active input field.

> **Status:** 0.12.0 released. The Windows installer, the portable archive, and the Linux packages
> are available. See the [0.12.0 release notes](doc/releases/0.12.0.md) for the changes.

**English** | [Русский](README.ru.md)

## Main features

- **Ctrl+Q** translates the selected text in a compact window next to the cursor.
- **Ctrl+Shift+Q** translates the selected text and replaces the selection in the active input
  field, which is convenient for replying in chats.
- **Ctrl+W** opens the main window with the selection translated.
- **Text checking** is a separate mode with the **Text checking** settings tab: LanguageTool finds
  spelling, grammar and punctuation issues, the main window highlights them and can replace one
  issue, all issues or copy the corrected text, and every replacement is one undo step. Online
  checking is opt-in; the public LanguageTool API and a self-hosted URL work without a key. The
  selection check in another application uses an optional fifth hotkey (empty by default) or
  `--proofread-selection` and replaces the selection only after confirmation.
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

- **[`qtranslate-0.12.0-win64-setup.exe`](https://github.com/youkaicat/QTranslate/releases/download/v0.12.0/qtranslate-0.12.0-win64-setup.exe)**. Installs QTranslate for the current user
  without administrator rights; by default it creates a Start menu shortcut and enables autostart,
  with an optional desktop shortcut. It also adds an uninstaller. Uninstalling keeps the settings
  in `%APPDATA%\qtranslate`.
- **[`qtranslate-0.12.0-win64.zip`](https://github.com/youkaicat/QTranslate/releases/download/v0.12.0/qtranslate-0.12.0-win64.zip)**. Extract it and run `bin\qtranslate.exe`;
  no installation or administrator rights are needed. The archive is self-contained: Qt and the
  MSVC runtime are included.
The installer is not code-signed yet, so Windows SmartScreen may show a warning.

The Linux packages are built for Debian 13 (trixie):
**[`qtranslate-0.12.0-linux-x86_64.tar.gz`](https://github.com/youkaicat/QTranslate/releases/download/v0.12.0/qtranslate-0.12.0-linux-x86_64.tar.gz)**,
**[`qtranslate_0.12.0_amd64.deb`](https://github.com/youkaicat/QTranslate/releases/download/v0.12.0/qtranslate_0.12.0_amd64.deb)**,
and
**[`qtranslate-0.12.0-x86_64.AppImage`](https://github.com/youkaicat/QTranslate/releases/download/v0.12.0/qtranslate-0.12.0-x86_64.AppImage)**
(use `--appimage-extract-and-run` when FUSE is unavailable).
They require glibc 2.38 or newer and do not run on Astra Linux 1.8.7. Astra Linux is supported through
a separate native build, `qtranslate-<version>-linux-x86_64-astra.tar.gz` and
`qtranslate_<version>+astra1_amd64.deb`; those are local verification artifacts and are not published
by CI. See [Linux](#linux-0120) for build and packaging steps.

## Usage

```
qtranslate [--minimized] [--verbose]
qtranslate [--minimized] [--verbose] --translate-popup|--translate-main|--translate-inplace|--proofread-selection
```

- `Ctrl+Q` — translate the selection in a popup next to the cursor.
- `Ctrl+Shift+Q` — translate the selection and replace it in the active input field.
- `Ctrl+W` — open the main window with the selection translated.
- `Ctrl+Enter` — translate in the main window.
- **Check** in the main window runs LanguageTool over the source text; **Settings → Text checking**
  chooses the language, categories, a self-hosted URL and the online gate. The optional
  **Check the selection in another application** hotkey (empty by default) checks the selected text
  and replaces it only after confirmation.
- Settings, service selection, **Show window** and **Quit** are available from the tray menu.
  Hotkeys, languages, proxy, autostart and API keys are configured in **Settings**.
- Global hotkeys are released automatically while a fullscreen application is in the foreground,
  so games receive Ctrl+Q/Ctrl+W. This can be disabled in **Settings → Hotkeys**; **Tray → Pause
  hotkeys** disables them manually.
- `--translate-popup`, `--translate-main`, `--translate-inplace` and `--proofread-selection` run the
  four scenarios in an already running instance (or start one). They are mutually exclusive and are
  the portable trigger for Wayland and desktop shortcuts; see [Linux](#linux-0120).
- `--minimized` starts hidden in the tray; `--verbose` writes a detailed log. On Windows the log is
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`; on Linux it is
  `$XDG_STATE_HOME/qtranslate/qtranslate.log` (`~/.local/state/...` when unset).
- Settings are `%APPDATA%\qtranslate\qtranslate.ini` on Windows and
  `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` on Linux (`~/.config/...` when unset).
- Portable mode: put an empty `qtranslate.ini` next to `qtranslate.exe` and the application will
  use it instead of `%APPDATA%\qtranslate\qtranslate.ini`.

## Translation providers

- **Google Translate** — free public endpoint or Cloud Translation API with a key. Choose the mode
  in **Settings → Services → Google Translate**.
- **DeepL** — official API with a key or the unofficial free web mode without a key. Both modes are
  in **Settings → Services → DeepL**.
- **Free web services** — MyMemory is a free no-key API; Mozhi, Yandex web, Bing web and Reverso
  are unofficial no-key endpoints. All are optional and gated by **Settings → Services → Allow free
  web services**; the unofficial ones may be rate-limited, change or stop working. Google and DeepL
  remain the primary providers.
- **Fallback service** — one optional free web service that retries a failed request once
  (**Settings → Services → Fallback service**). It is off by default, needs the free web services
  gate, and is used only on a retryable failure (auth, quota, rate limit, network or timeout); it
  never loops and the original error is shown when the fallback also fails. The text is sent to the
  fallback service as well.
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
   ollama pull bonsai-2:latest
   ```
3. In QTranslate open **Settings → Services → OpenAI-compatible provider** and set:
   - Server URL: `http://localhost:11434/v1`
   - API key: leave empty
   - Model: the name from `ollama list`, for example `bonsai-2:latest`
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

### Linux (0.12.0)

QTranslate builds, links, and passes 45 tests (36 shared plus 9 Linux platform tests) in Debug and
Release with `-Wall -Wextra` and no warnings on Debian 13 (trixie, Qt 6.8.2) and Astra Linux 1.8.7
(Qt 6.10.2). X11 is the full target: global hotkeys, selection capture, in-place replacement,
autostart, the four CLI commands, and tray integration are implemented. Wayland is limited: the
`GlobalShortcuts` portal when the desktop provides it, manual copying, the CLI commands, and explicit
warnings; there is no synthetic-input fallback. GNOME needs an AppIndicator extension for the tray
icon.

Build and test on Debian 13 (the package list lives in `packaging/linux/trixie-packages.txt`, which
the Dockerfile and CI share):

```bash
sudo apt install build-essential cmake ninja-build git ca-certificates
grep -vE '^[[:space:]]*(#|$)' packaging/linux/trixie-packages.txt | \
  xargs sudo apt install -y --no-install-recommends
cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-release
xvfb-run -a ctest --test-dir build/linux-release --output-on-failure
```

`xvfb-run` needs `xauth`, and `tst_hotkeysequence` opens a window, so a bare `ctest` has no display.
Packaging needs Debian 13 (trixie). On a trixie host, run the script after the build above:

```bash
./packaging/linux/package.sh --build-dir build/linux-release --output-dir build/artifacts-linux
```

On another host, build and package inside the container built from
`packaging/linux/trixie.Dockerfile`; the build must use the container's Qt in a separate build
directory:

```bash
docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .
docker run --rm -v "$PWD:/src" -w /src -e HOME=/tmp \
  --user "$(id -u):$(id -g)" qtranslate-trixie bash -c '
  cmake -S . -B build/linux-trixie -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr &&
  cmake --build build/linux-trixie &&
  ./packaging/linux/package.sh --build-dir build/linux-trixie --output-dir build/artifacts-linux'
```

The script builds the portable archive, `.deb`, AppImage, and `SHA256SUMS-linux.txt`; pass
`--skip-appimage` only for an explicit offline run. On Astra Linux build natively with Qt 6.10.2 and
use `--target astra`, which produces the `-astra.tar.gz`, the `+astra1_amd64.deb`, and
`SHA256SUMS-linux-astra.txt`:

```bash
./packaging/linux/package.sh --target astra --build-dir build/linux-release --output-dir build/artifacts-linux
```

The trixie packages require glibc 2.38+ and do not run on Astra Linux 1.8.7; the native Astra build
exists for that reason.

#### Astra Linux: global hotkeys

Astra Linux's XPARSEC X11 protection denies passive `XGrabKey` grabs to untrusted binaries, so
global hotkeys do not work in a normally installed package until the X server trusts the binary.
Once QTranslate is installed from the package, add `/usr/bin/qtranslate` to the trusted-client list
as root:

```bash
sudo mkdir -p /etc/X11/trusted.d
echo '/usr/bin/qtranslate(KBD_R)' | sudo tee /etc/X11/trusted.d/qtranslate
sudo chown root:root /etc/X11/trusted.d/qtranslate
sudo chmod 644 /etc/X11/trusted.d/qtranslate
sudo /usr/bin/fly-set-execid.sh
getfattr -n security.exec_id /usr/bin/qtranslate   # from attr; expect 0sEA== (0x10, KBD_R)
```

Restart QTranslate; log out and back in if the X server has already cached the client list.

`KBD_R` allows the process to read and grab all keyboard input — exactly the capability XPARSEC
restricts. Grant it only to the root-owned `/usr/bin/qtranslate` from the package and only with the
security policy's approval. To revoke it, run
`sudo setfattr -x security.exec_id /usr/bin/qtranslate` and remove
`/etc/X11/trusted.d/qtranslate`.

## Known limitations

- Selection capture does not work in windows of processes started as administrator.
- The free Google endpoint can answer `HTTP 429` when it rate-limits a client; wait or use Google
  Cloud/DeepL with an API key.
- DeepL free web mode is an unofficial endpoint without an API key. It may be rate-limited, change
  or stop working; the official DeepL API is more reliable.
- Mozhi, Yandex web, Bing web and Reverso are unofficial endpoints without an API key. They may be
  rate-limited, change or stop working; MyMemory is a free no-key API with its own quota and a
  500-byte request limit. Reverso supports only English, French, German, Spanish, Italian and
  Russian and cannot detect the source language. All of them send the text to a third party.
- OpenRouter and other OpenAI-compatible providers need an API key and an available model. Quality,
  speed and cost depend on the chosen model; free models have strict rate limits. The text is sent
  to the selected provider.
- LanguageTool checking needs the online gate; the public API limits requests to about 20 per
  minute and 20 000 characters per request, and self-hosted LanguageTool is more reliable for long
  or frequent checks. The checked text goes to the LanguageTool server. Replacing a selection in
  another application needs confirmation, undo depends on that application, and a changed foreground
  window falls back to copying the corrected text to the clipboard.
- The intermediate `Ctrl+C` used to grab a selection may appear in the Windows clipboard history
  (`Win+V`); the original clipboard contents are restored. This is Windows-specific.
- The installer is not code-signed yet.
- The Hindi interface is translated, but Qt 6.12 has no Hindi catalog, so standard Qt dialogs
  remain English.
- On Linux, X11 is fully supported. Wayland support is limited: global hotkeys require the
  `GlobalShortcuts` portal; otherwise use the CLI commands or a desktop shortcut, copy the selection
  manually, and note that in-place mode writes the translation to the clipboard. There is no
  synthetic-input fallback (`ydotool`/`wtype`).
- On Wayland, in-place replacement and active-window detection are not guaranteed; Ctrl+Shift+Q
  copies the translation instead of pasting it.
- GNOME needs the AppIndicator extension to show the tray icon; without a tray the application warns
  and keeps the main window.
- Linux secrets use libsecret when it is available and the keyring is running; otherwise a `0600`
  file is used with a warning in settings.
- The trixie Linux packages require glibc 2.38+ and do not run on Astra Linux 1.8.7. Astra uses the
  native `-astra.tar.gz`/`+astra1` packages.

The text being translated is sent to the selected provider: Google, DeepL, OpenRouter or another
OpenAI-compatible server. A local server such as Ollama keeps the text on your machine. QTranslate
does not write the texts to its log.

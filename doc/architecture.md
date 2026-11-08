# qtranslate — architecture

This document describes the design of the qtranslate desktop translator: which modules it consists of,
how they are connected, and how the main scenarios flow. It is the reference document for the
implementation: if the code diverges from it, either fix the code or deliberately update the document.

---

## 1. Goals and requirements

### 1.1. Functional requirements

| # | Requirement |
|---|-------------|
| F1 | Starts minimized in the system tray; closing the window hides it to the tray instead of quitting the program. |
| F2 | Starts with Windows (enabled in settings). On autostart the program starts directly in the tray. |
| F3 | Translates text selected in any program via a global hotkey. |
| F4 | **Ctrl+Q** — translate the selected text in a popup window next to the cursor. |
| F5 | **Ctrl+Shift+Q** — translate the selected text and replace the selection in the active input field with it (no popup and no manual copying). |
| F6 | **Ctrl+W** — translate the selected text in the main window (the window opens and becomes active). |
| F7 | Hotkeys are configurable; Ctrl+Q / Ctrl+Shift+Q / Ctrl+W are the defaults. |
| F8 | Translation services: Google Translate and DeepL with api/free modes, an OpenAI-compatible provider; the active service is chosen in settings and in the tray. |
| F9 | Network requests can go through a proxy: HTTP (including for https requests, via `CONNECT`), SOCKS5 (with and without authentication), or the system proxy. |

### 1.2. Non-functional requirements

- **Fast.** Cold start under 300 ms until the tray icon appears. From hotkey press to the popup with a
  spinner — no more than 150 ms; after that the network determines the time.
- **Light.** One process, one GUI thread, no background services. Idle ~0% CPU and less than 30 MB of memory.
- **Unobtrusive.** The program must not spoil the user's clipboard and must not steal focus without
  an explicit user action.
- **Extensible.** A new translation service is added with one class without changing the rest of the code.
- **Testable.** Logic without UI and WinAPI (parsing service responses, languages, text splitting,
  settings) is covered by unit tests.

### 1.3. Technology stack

- C++17, Qt 6.5+ (Windows: Qt 6.12, MSVC 2022 x64; Linux: GCC 12.2 on Astra Linux and GCC 14.2 in
  trixie with Qt 6.10.2/6.8.2; Clang 16 is also clean), CMake ≥ 3.19.
- Qt modules: `Core`, `Gui`, `Widgets`, `Network`; `DBus` on Linux for the portal and the tray. Tests
  additionally use `Test`.
- Platforms: **Windows 10/11 x64** and **Linux x86_64 (0.12.0)**; macOS is not planned. Platform code
  is isolated (see §3.3 and §4.14) so the port does not touch the core.
- TLS: on Windows the stock Qt 6 Schannel backend (`qschannelbackend`), OpenSSL is not needed; on Linux
  the Qt OpenSSL backend is used.
- WinAPI (Windows only): `user32` (hotkeys, `SendInput`, clipboard), `crypt32` (DPAPI for secrets).

### 1.4. Platform matrix

| Component | Windows 10/11 x64 (0.11.0) | Linux x86_64 (0.12.0) |
|---|---|---|
| Build | MSVC 2022, Qt 6.12 | GCC/Clang, Qt 6.5+ (trixie 6.8.2, Astra 6.10.2), CMake |
| UI | Qt Widgets, tray | Qt Widgets, StatusNotifierItem/AppIndicator |
| Hotkeys | `RegisterHotKey` | X11 `XGrabKey`; Wayland — the `GlobalShortcuts` portal or CLI + desktop shortcuts |
| Selection capture | `SendInput` Ctrl+C + clipboard | X11 XTest; Wayland — the user copies manually |
| Pasting the translation | `SendInput` Ctrl+V | X11 XTest; Wayland — not reliably available |
| Autostart | `Run` registry key | XDG autostart |
| Secrets | DPAPI | libsecret; a 0600 fallback file |
| Settings | `%APPDATA%\qtranslate` | `$XDG_CONFIG_HOME/qtranslate` |
| Packages | installer + portable zip | trixie `tar.gz`, `.deb`, AppImage; native Astra `-astra.tar.gz` and `+astra1` `.deb` |

Windows-specific limitations (elevated windows, Win+V, DPI) do not apply on Linux; instead, Wayland
limitations appear (§4.14).

---

## 2. General scheme

```
                         ┌─────────────────────────────┐
                         │           main.cpp          │
                         │ QApplication, SingleInstance│
                         └──────────────┬──────────────┘
                                        │ creates
                                        ▼
┌───────────────────────────────────────────────────────────────────────────┐
│                               AppController                               │
│  wires modules and runs scenarios (Ctrl+Q, Ctrl+W, applying settings)     │
└──┬──────────┬──────────────┬───────────────┬─────────────┬────────────┬───┘
   │          │              │               │             │            │
   ▼          ▼              ▼               ▼             ▼            ▼
┌──────┐ ┌──────────┐ ┌─────────────┐ ┌──────────────┐ ┌────────┐ ┌──────────┐
│ UI   │ │ Hotkey   │ │ Selection   │ │ Translation  │ │Settings│ │Autostart │
│      │ │ Manager  │ │ Grabber     │ │ Manager      │ │ Store  │ │          │
│Tray  │ │(WinAPI)  │ │(WinAPI)     │ │ cache, langs,│ │ INI +  │ │(registry)│
│Main  │ └──────────┘ └─────────────┘ │ cancel       │ │ DPAPI  │ └──────────┘
│Popup │                              └──────┬───────┘ └────────┘
│Setts │                                     │ TranslationService (interface)
└──────┘                           ┌─────────┴──────────┐
                                   ▼                    ▼
                          ┌─────────────────┐  ┌─────────────────┐
                          │ GoogleTranslate │  │ DeepLService    │
                          │ Service         │  │                 │
                          └────────┬────────┘  └────────┬────────┘
                                   └─────────┬──────────┘
                                             ▼
                                  ┌─────────────────────┐
                                  │   NetworkManager    │
                                  │QNetworkAccessManager│
                                  │ + proxy, timeouts   │
                                  └─────────────────────┘
```

Dependency rules (top to bottom, no reverse dependencies):

1. `ui/` knows about `core/` (types, languages) but not about services, network, or WinAPI.
2. `services/` know about `core/` and `net/` but not about the UI.
3. `platform/` knows nothing but Qt.
4. Only `AppController` knows all modules at once. Modules communicate through it via signals/slots.

### 2.1. Thread model

Everything runs in **one GUI thread**. Qt networking is asynchronous (`QNetworkReply`), clipboard waiting
uses a `QTimer`, so extra threads are not needed. No operation may block the event loop: no
`waitForFinished`, `QEventLoop::exec()` inside handlers, or `Sleep()`.

---

## 3. Source structure

### 3.1. Directory tree

```
qtranslate/
├── CMakeLists.txt
├── .github/
│   └── workflows/build.yml     build, tests, packaging and release
├── packaging/
│   ├── windows/
│   │   ├── qtranslate.iss      Inno Setup installer
│   │   └── package.ps1         dist + zip + setup + SHA256SUMS
│   └── linux/
│       ├── package.sh          tar.gz, .deb, AppImage; --target astra
│       ├── qtranslate.desktop  freedesktop desktop entry
│       ├── trixie.Dockerfile   Debian 13 build/packaging image
│       └── trixie-packages.txt shared apt package list for the image and CI
├── doc/
│   ├── architecture.md
│   ├── handoff.md
│   ├── code-signing.md
│   ├── dictionary.md
│   ├── free-endpoints.md
│   ├── llm-actions.md
│   ├── local-translation.md
│   ├── proofreading.md
│   ├── releases/
│   │   ├── 0.9.0.md            release notes
│   │   ├── 0.9.1.md
│   │   ├── 0.10.0.md
│   │   └── 0.11.0.md
│   └── roadmap/
│       ├── README.md           map list
│       └── qtranslate/
│           ├── plan.md         active roadmap
│           ├── legacy-plan.md  Russian historical archive of the old flat plan
│           ├── journal.md
│           ├── 01-documentation-migration/    ...
│           ├── 02-linux/                      ...
│           ├── 03-free-web-endpoints/         ...
│           ├── 04-local-offline-translation/  ...
│           ├── 05-proofreading/               ...
│           ├── 06-llm-actions/                ...
│           └── 07-dictionary/                 ...
├── resources/
│   ├── icons/                  app.ico, app.png
│   └── qtranslate.rc           exe icon and version
├── i18n/                       UI translations, one .ts per shipped language
│   ├── README.md
│   ├── qtranslate_ru_RU.ts
│   ├── qtranslate_es.ts
│   ├── qtranslate_de.ts
│   ├── qtranslate_fr.ts
│   ├── qtranslate_pt_BR.ts
│   ├── qtranslate_it.ts
│   ├── qtranslate_pl.ts
│   ├── qtranslate_uk.ts
│   ├── qtranslate_tr.ts
│   ├── qtranslate_zh_CN.ts
│   ├── qtranslate_ja.ts
│   ├── qtranslate_ko.ts
│   ├── qtranslate_ar.ts
│   └── qtranslate_hi.ts
├── src/
│   ├── main.cpp
│   ├── ai/
│   │   └── openaichat.h/.cpp         OpenAI-compatible Chat Completions transport
│   ├── app/
│   │   ├── appcontroller.h/.cpp
│   │   ├── commandline.h/.cpp        CLI parsing and command enum
│   │   ├── logging.h/.cpp            qtranslate.app logging category
│   │   └── singleinstance.h/.cpp
│   ├── core/                   ← static library qtranslate_core (no Widgets or WinAPI)
│   │   ├── appinfo.h/.cpp            application name and version
│   │   ├── apppaths.h                XDG config/state paths on Linux, QStandardPaths on Windows
│   │   ├── translationtypes.h        TranslationRequest/Result/Error
│   │   ├── translationservice.h/.cpp service interface + TranslationReply
│   │   ├── translationmanager.h/.cpp service registry, cache, language logic, cancellation
│   │   ├── languages.h/.cpp          language table and code mapping
│   │   ├── uilanguages.h/.cpp        UI language codes, native names, resource names
│   │   ├── popupplacement.h/.cpp     popup positioning at the cursor
│   │   ├── textchunker.h/.cpp        splitting long text into chunks
│   │   ├── settings.h/.cpp           AppSettings + SettingsStore
│   │   └── hotkeysequence.h/.cpp     parsing/serializing key combinations
│   ├── net/
│   │   └── networkmanager.h/.cpp     QNAM, proxy, timeouts, User-Agent
│   ├── services/
│   │   ├── googletranslateservice.h/.cpp
│   │   ├── googleparser.h/.cpp       pure response parsing functions
│   │   ├── deeplservice.h/.cpp
│   │   ├── deeplparser.h/.cpp
│   │   ├── deeplwebservice.h/.cpp    unofficial free web endpoint
│   │   ├── deeplwebparser.h/.cpp     pure parsing for the web endpoint
│   │   ├── queuedwebservice.h/.cpp   serialized per-host request queue
│   │   └── openaicompatibleservice.h/.cpp  OpenAI-compatible provider
│   ├── platform/
│   │   ├── hotkeymanager.h           platform-independent interface
│   │   ├── hotkeymanager_win.cpp     RegisterHotKey
│   │   ├── hotkeymanager_linux.cpp   XGrabKey and the GlobalShortcuts portal
│   │   ├── sessiontype.h             X11 vs Wayland session detection
│   │   ├── portalshortcut.h          shared (sa{sv}) D-Bus type
│   │   ├── selectiongrabber.h
│   │   ├── selectiongrabber_win.cpp  SendInput Ctrl+C
│   │   ├── selectiongrabber_linux.cpp XTest/clipboard; Wayland clipboard read
│   │   ├── autostart.h
│   │   ├── autostart_win.cpp         HKCU Run
│   │   ├── autostart_linux.cpp       XDG Desktop Entry
│   │   ├── secretprotector.h
│   │   ├── secretprotector_win.cpp   DPAPI
│   │   ├── secretprotector_linux.cpp libsecret or a 0600 fallback file
│   │   ├── windowactivation.h        right to bring a window to the foreground
│   │   ├── windowactivation_win.cpp  AllowSetForegroundWindow
│   │   ├── windowactivation_linux.cpp _NET_ACTIVE_WINDOW/XGetInputFocus
│   │   └── *_stub.cpp                stubs for platforms without a backend
│   └── ui/
│       ├── trayicon.h/.cpp
│       ├── mainwindow.h/.cpp
│       ├── popupwindow.h/.cpp
│       ├── settingsdialog.h/.cpp
│       ├── languagecombobox.h/.cpp
│       ├── translationerrortext.h/.cpp  user-facing translation error text
│       └── appicon.h/.cpp
└── tests/
    ├── CMakeLists.txt
    ├── fixtures/                     saved Google/DeepL responses (google/, deepl/)
    ├── tst_appinfo.cpp
    ├── tst_autostart.cpp               Windows and Linux variants
    ├── tst_cli.cpp                     Linux: parser and two-process forwarding
    ├── tst_deeplparser.cpp
    ├── tst_deeplservice.cpp
    ├── tst_deeplwebparser.cpp
    ├── tst_deeplwebservice.cpp
    ├── tst_googleparser.cpp
    ├── tst_googletranslateservice.cpp  service against a local HTTP server
    ├── tst_hotkeymanager.cpp           Windows-only
    ├── tst_hotkeymanager_x11.cpp       Linux X11 backend
    ├── tst_hotkeymanager_wayland.cpp   Linux portal backend against a mock DBus
    ├── tst_hotkey_selection_x11.cpp    Linux real manager + grabber
    ├── tst_hotkeysequence.cpp
    ├── tst_languages.cpp
    ├── tst_networkmanager.cpp
    ├── tst_openaichat.cpp
    ├── tst_openaicompatibleservice.cpp
    ├── tst_popupplacement.cpp
    ├── tst_popupwindow.cpp
    ├── tst_queuedwebservice.cpp        serialized request queue with a fake service
    ├── tst_secretprotector.cpp         Windows and Linux variants
    ├── tst_selectiongrabber_linux.cpp  Linux selection capture
    ├── tst_settings.cpp
    ├── tst_settingsdialog.cpp
    ├── tst_textchunker.cpp
    ├── tst_translationmanager.cpp
    ├── tst_translations.cpp            shipped .ts files: completeness and placeholders
    ├── tst_uilanguages.cpp             UI language table
    └── tst_windowactivation_linux.cpp  Linux window helpers
```

### 3.2. Mapping to the current `CMakeLists.txt`

The §3.1 tree shows the repository's current layout, and the table below maps it to the current
targets. Currently:

| Target | What is included now (0.12.0) |
|---|---|
| `qtranslate_core` | `ai/openaichat`; `core/`: `appinfo`, `apppaths`, `hotkeysequence`, `languages`, `uilanguages`, `popupplacement`, `settings`, `textchunker`, `translationtypes`, `translationservice`, `translationmanager` (cache, smart language); `net/networkmanager`; `services/`: `googleparser`, `googletranslateservice`, `deeplparser`, `deeplservice`, `deeplwebparser`, `deeplwebservice`, `openaicompatibleservice`, `queuedwebservice` |
| `qtranslate` | `main.cpp`, `app/*` (including `commandline` and the file log), `ui/*` (`appicon`, `languagecombobox`, `mainwindow` with auto-translation, `popupwindow`, `settingsdialog`, `translationerrortext`, `trayicon`), `platform/*` (Windows and Linux backends; `Qt6::DBus` is linked on Linux), `resources/qtranslate.rc`, `resources/icons/app.ico`, `i18n/qtranslate_*.ts` |
| tests (`tests/CMakeLists.txt`) | `tst_appinfo`, `tst_autostart` (Windows/Linux), `tst_cli` (Linux), `tst_deeplparser`, `tst_deeplservice`, `tst_deeplwebparser`, `tst_deeplwebservice`, `tst_googleparser`, `tst_googletranslateservice`, `tst_hotkeymanager` (Windows), `tst_hotkeymanager_x11`/`tst_hotkeymanager_wayland` (Linux), `tst_hotkey_selection_x11` (Linux), `tst_hotkeysequence`, `tst_languages`, `tst_networkmanager`, `tst_popupplacement`, `tst_popupwindow` (offscreen), `tst_queuedwebservice`, `tst_secretprotector` (Windows/Linux), `tst_selectiongrabber_linux`, `tst_settings`, `tst_settingsdialog` (offscreen), `tst_textchunker`, `tst_translationmanager` (cache and smart retry), `tst_translations`, `tst_uilanguages`, `tst_windowactivation_linux`, `tst_openaichat`, `tst_openaicompatibleservice`; 24 tests on Windows and 29 on Linux. Each test is added by the `qtranslate_add_test(<name>)` function, links against `qtranslate_core`, and receives the `tests/fixtures` path in the `QTRANSLATE_FIXTURES_DIR` macro |

### 3.3. Build

- `qtranslate_core` — a `STATIC` library from `ai/`, `core/`, `net/`, `services/`; depends on `Qt::Core`,
  `Qt::Gui` (because of `QKeySequence` in `hotkeysequence`) and `Qt::Network`, but not on `Qt::Widgets`
  and does not use WinAPI. Both the application and the tests link against it.
- `qtranslate` — the executable (`WIN32` on Windows): `app/`, `platform/`, `ui/`, resources; links
  against `qtranslate_core` and `Qt::Widgets`, plus `user32`/`crypt32` on Windows and `Qt6::DBus` on
  Linux.
- Platform files are selected per platform: `*_win.cpp` on Windows, `*_linux.cpp` on Linux, and a
  stub only for platforms without a backend. Headers in `platform/` do not contain `<windows.h>` —
  WinAPI is visible only in `*_win.cpp`.
- UI translation: `qt_add_translations(qtranslate TS_FILES <all i18n/qtranslate_*.ts>)`, the `.qm`
  files are embedded in resources.
- `enable_testing()` + `add_subdirectory(tests)`, each test is a separate `qt_add_executable` and
  `add_test`.
- Deployment uses `qt_generate_deploy_app_script` (windeployqt) on Windows only. The distribution includes the
  `tls/qschannelbackend.dll` plugin and the MSVC CRT (`vcruntime140*.dll`, `msvcp140*.dll`,
  `concrt140.dll`): `windeployqt` does not find the runtime without `VCINSTALLDIR`, so CMake copies it
  itself. This is needed to run on Windows without the Visual C++ Redistributable installed.
  `windeployqt` itself is invoked with `NO_COMPILER_RUNTIME`,
  `--no-system-d3d-compiler`/`--no-system-dxc-compiler`, `--no-opengl-sw`, and
  `--translations ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar`: otherwise, with `VCINSTALLDIR` set, it
  adds `vc_redist.x64.exe` (~18 MB), the Direct3D/DXC compilers, and software OpenGL
  `opengl32sw.dll` (~20 MB), which Qt Widgets does not use. Qt translations are needed for standard
  dialogs and menus; Qt 6.12 has no file for Hindi, so those remain English. The qtranslate UI
  translation is embedded in the exe.
- On Linux the Windows deploy block is skipped with `if(WIN32)`; CMake installs `bin/qtranslate`,
  `share/applications/qtranslate.desktop`, and `share/icons/hicolor/256x256/apps/qtranslate.png`.
  `packaging/linux/package.sh` owns the bundle: Qt libraries, plugins, Qt translations, `qt.conf`,
  and RPATH. The install tree keeps `RUNPATH $ORIGIN:$ORIGIN/../lib`.

### 3.4. Packaging, CI and release

The project version is defined once — `project(qtranslate VERSION 0.11.0)`; `AppInfo::version()`, the
`User-Agent`, the exe version resource, and artifact names are derived from it. The release tag is
`v0.11.0`; CI compares it with `CMakeLists.txt` and fails on a mismatch.

**Portable archive.** `cmake --install <build> --prefix build/dist` creates a self-contained `bin/`,
`plugins/`, `translations/` directory with Qt, plugins, and the MSVC CRT. `package.ps1` checks the
required files and packs the contents of `dist` into `qtranslate-<version>-win64.zip`; the archive
contains the same `bin/`, `plugins/`, `translations/`, and is started via `bin\qtranslate.exe`.

**Linux packages.** `packaging/linux/package.sh` builds from a trixie Release tree or the
`qtranslate-trixie` image: `qtranslate-<version>-linux-x86_64.tar.gz` (bundled Qt 6.8.2, plugins,
Qt translations, `qt.conf`, `patchelf` RPATH), `qtranslate_<version>_amd64.deb` (three installed
files, no maintainer scripts, `Depends` from `dpkg-shlibdeps`), and
`qtranslate-<version>-x86_64.AppImage` (pinned `linuxdeploy`, `appimagetool`, and runtime), plus
`SHA256SUMS-linux.txt`. `--skip-appimage` is for explicit offline runs only; CI always builds the
AppImage. On Astra Linux `--target astra` produces
`qtranslate-<version>-linux-x86_64-astra.tar.gz` and `qtranslate_<version>+astra1_amd64.deb` with
Astra's Qt 6.10.2 and Astra `Depends`, plus `SHA256SUMS-linux-astra.txt`; these are local
verification artifacts and are not CI-published. Trixie artifacts require glibc 2.38+ and are never
claimed to run on Astra.

**Installer.** `packaging/windows/qtranslate.iss` (Inno Setup 6.3+ / 7) installs the application
**for the current user**: `PrivilegesRequired=lowest`, the
`%LOCALAPPDATA%\Programs\qtranslate` directory, an uninstaller. UAC is not requested. Installer
languages are English and Russian. Installer tasks:

- **Start menu shortcut** — enabled by default; creates the application shortcut and the uninstaller
  shortcut.
- **Desktop shortcut** — disabled by default.
- **Autostart** — enabled by default; writes `HKCU\...\Run` with the line
  `"…\qtranslate.exe" --minimized` and synchronizes `app/autostart` in
  `%APPDATA%\qtranslate\qtranslate.ini` so the first application launch does not remove the entry.
  On update the checkbox state is taken from this setting; on uninstall the `Run` entry is removed,
  and the settings are kept.

The installer and uninstaller check `AppMutex=Local\qtranslate-app`, which the running application
(`main.cpp`) holds, so files are not replaced or deleted under a running copy.
`CloseApplications=yes` additionally lets Restart Manager close the application if the user agrees.

**Script.** `packaging/windows/package.ps1` runs `cmake --install`, checks for `qtranslate.exe`, the
Qt DLLs, `qwindows.dll`, `qschannelbackend.dll`, and the CRT, builds the zip, the installer, and
`SHA256SUMS.txt`. The `-SkipInstall` and `-SkipInstaller` parameters are for debugging packaging; CI
uses the full path. `SHA256SUMS.txt` remains a release asset but is not listed in the README and the
Downloads section.

**CI.** `.github/workflows/build.yml` has two build jobs:
1. Windows (`windows-latest`): `install-qt-action` installs Qt 6.8.3 LTS MSVC 2022 x64 (aqtinstall
   does not yet support the Qt 6.12 repository layout; local development uses 6.12, the project
   requires 6.5+); `ilammy/msvc-dev-cmd` sets up MSVC; CMake (Ninja) builds Release; `ctest` runs
   with `QT_QPA_PLATFORM=offscreen`; Chocolatey installs Ninja and Inno Setup; `package.ps1` builds
   the artifacts; `upload-artifact` stores `build/artifacts/*` as `qtranslate-windows-x64`.
2. Linux (`Linux x64 (trixie)` on `ubuntu-latest`, `container: debian:trixie`): `git` and
   `ca-certificates` are installed before checkout, then the shared list from
   `packaging/linux/trixie-packages.txt`; CMake Release with `-DCMAKE_PREFIX_PATH=/usr`; build;
   `timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure`; `package.sh` without
   `--skip-appimage`; `upload-artifact` stores `build/artifacts-linux/*` as
   `qtranslate-linux-x64`.

The workflow runs on push to `main`, on pull requests, and manually. On a `v*` tag the release job
with `contents: write` needs both build jobs, downloads both artifact sets into `artifacts`, and
creates/updates the GitHub Release with the installer, the zip, `SHA256SUMS.txt`, the Linux tar.gz,
the `.deb`, the AppImage, and `SHA256SUMS-linux.txt`; the notes come from `doc/releases/<version>.md`.
The checksum files remain assets but are not advertised in the README/Downloads. A repeated run
overwrites the release files (`gh release upload --clobber`). The real GitHub run and a tag
publication require the user's explicit approval and are not part of local verification.

**Signing.** There is no code signing yet: the project has no certificate, and SmartScreen may show a
warning. Adding signing does not change the artifact format or the CI steps (it needs a secret with
the certificate and a `signtool` invocation).

---

## 4. Modules

### 4.1. `main.cpp` and `SingleInstance`

`main()`:

1. Creates `QApplication`, sets `applicationName`/`applicationVersion`,
   `setQuitOnLastWindowClosed(false)` — otherwise hiding the last window would end the process.
   `organizationName` is deliberately not set so that data lives in `%APPDATA%\qtranslate`
   (see §4.11). On Windows a named mutex `Local\qtranslate-app` is additionally created; the installer
   and uninstaller check it (`AppMutex`, §3.4).
2. Parses the arguments (`QCommandLineParser`): `--minimized` (passed from autostart) and
   `--verbose` (verbose log).
3. `SingleInstance` (key `qtranslate-<user name>`):
   - The primary instance is elected by taking the lock file `%TEMP%\qtranslate-<user>.lock`
     (`QLockFile`). This is atomic, and the lock of a crashed process is detected by PID and taken
     over. A `QLocalServer` alone is not enough: there is a race window between a failed connect
     attempt and `listen()` if two instances start at the same time.
   - Failed to take the file — the program is already running: connect to its `QLocalServer`
     (retrying for up to 1 s if the server is not up yet), send `activate` (or `noop` with
     `--minimized`), and exit with code 0. Before `activate`, `AllowSetForegroundWindow(ASFW_ANY)` is
     called so the primary instance can bring the window to the foreground.
   - Taken — start the `QLocalServer` and show the main window on the `activate` command.
4. Loads settings and installs a `QTranslator` for the UI language.
5. Creates `AppController` and starts the event loop.

Without second-instance protection, starting again from the shortcut (or autostart plus a manual
start) would produce two processes that would not share the hotkeys.

### 4.2. `AppController`

The only class that knows about all modules. It owns them (through a `QObject` tree or
`std::unique_ptr`) and implements the scenarios from §5.

Responsibilities:

- creating the modules in the right order (§5.1);
- handling `HotkeyManager::activated(action)` → selection capture → translation → showing the result;
- applying changed settings (`SettingsStore::changed`): re-registering hotkeys, changing the proxy,
  syncing autostart, switching the service;
- showing tray notifications (hotkey taken, autostart error, etc.);
- clean shutdown: unregistering hotkeys, saving window geometry.

`AppController` contains no translation business logic (that is in `TranslationManager`) and does not
draw UI.

### 4.3. Hotkeys — `HotkeyManager`

```cpp
enum class HotkeyAction { TranslateToPopup, TranslateAndReplace, TranslateToMainWindow,
                           ShowMainWindow };

class HotkeyManager : public QObject {
    Q_OBJECT
public:
    // false — the combination is taken by another program or is not supported
    bool registerHotkey(HotkeyAction action, const QKeySequence &seq);
    void unregisterHotkey(HotkeyAction action);
    void unregisterAll();
    // Temporarily disable everything: entering a new combination, a manual pause, or
    // a fullscreen application in the foreground.
    void setSuspended(bool suspended);
    bool isSuspended() const;
    QKeySequence hotkey(HotkeyAction action) const;
signals:
    void activated(HotkeyAction action);
    // The reason is the WinAPI text (for example, "Hot key is already registered")
    void registrationFailed(HotkeyAction action, const QKeySequence &seq,
                            const QString &reason);
};
```

Windows implementation (`hotkeymanager_win.cpp`):

- `RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk)`. With `hWnd = nullptr` the `WM_HOTKEY`
  message arrives in the thread queue; `QAbstractNativeEventFilter` installed via
  `QCoreApplication::installNativeEventFilter` catches it (event type `"windows_generic_MSG"`).
- `MOD_NOREPEAT` excludes auto-repeat when the key is held down.
- The `QKeySequence` → `(MOD_*, VK_*)` translation is in `core/hotkeysequence` (a pure function covered
  by tests): letters and digits match ASCII, `F1–F24`, `Insert`, `Pause`, etc. — by table.
- If `RegisterHotKey` returns an error (`ERROR_HOTKEY_ALREADY_REGISTERED`), the method returns `false`,
  emits `registrationFailed` with the error text, and `AppController` shows a tray notification
  suggesting another combination.
- The registration id is `1 + int(action)` (`RegisterHotKey` range 0x0001–0xBFFF). `setSuspended(true)`
  unregisters the hotkeys but remembers the combinations; `setSuspended(false)` registers them again
  and reports errors through the same signal.
- At startup three default combinations are registered: Ctrl+Q (`TranslateToPopup`), Ctrl+Shift+Q
  (`TranslateAndReplace`), and Ctrl+W (`TranslateToMainWindow`). `ShowMainWindow` is not assigned yet.
- `AppController` checks `Platform::isFullscreenForegroundWindow()` every 500 ms: if another
  application owns a fullscreen window (a game, fullscreen video), all global combinations are
  unregistered via `setSuspended(true)`. When the fullscreen window loses focus, they are registered
  again. This way a game gets Ctrl+Q/Ctrl+W instead of QTranslate. The check excludes QTranslate's own
  windows and maximized windows with a normal frame.
- The tray menu has a manual **Pause hotkeys** checkbox — it unregisters all combinations until it is
  enabled again (for example, if a game is not detected as fullscreen). The pause is not persisted
  between launches. Entering a new combination in settings also suspends registration; after the
  dialog closes, the state is recalculated taking the manual pause and fullscreen mode into account.
- The auto-pause is controlled by the `hotkeys/pauseInFullscreen` setting (default `true`, the
  **Hotkeys** tab). If it is disabled, the timer keeps running, but a fullscreen application no longer
  unregisters the combinations; the manual pause in the tray remains available.

Default settings:

| Action | Default |
|---|---|
| `TranslateToPopup` | `Ctrl+Q` |
| `TranslateAndReplace` | `Ctrl+Shift+Q` |
| `TranslateToMainWindow` | `Ctrl+W` |
| `ShowMainWindow` | not assigned |

> **Decision.** Global registration takes the combination from **all** programs: Ctrl+W will no
> longer close tabs in browsers and editors, Ctrl+Q will no longer close programs. The project owner
> does not use these combinations for closing, so Ctrl+Q / Ctrl+Shift+Q / Ctrl+W remain the defaults,
> without warnings or a first-run wizard. They can be changed in settings. In fullscreen applications
> the combinations are unregistered automatically, and for other cases there is a manual pause in the
> tray. Triggering "only when there is a selection" is impossible: at the moment of pressing it is
> unknown whether there is a selection.

### 4.4. Selected text capture — `SelectionGrabber`

Windows has no system-wide "selection", so the text is obtained through the clipboard: we simulate
Ctrl+C in the active window and read the result.

```cpp
class SelectionGrabber : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &text)>; // empty string — nothing selected
    void grab(Callback cb);       // asynchronous; a repeated call while grabbing is ignored
    bool isBusy() const;
    void cancel();                // stop grabbing and restore the clipboard
    void setMaxLength(int length);
    void setRestoreClipboard(bool restore);
};
```

Algorithm (a state machine on `QTimer`, no blocking):

1. **Waiting for key release.** The user is still holding Ctrl and W. Poll `GetAsyncKeyState` every
   10 ms until the modifiers are released, but no longer than 300 ms. If Alt/Shift/Win are still
   pressed, send a synthetic release for them; otherwise the application would get Ctrl+Alt+C and the
   like.
2. **Clipboard snapshot.** Copy all formats of the current `QMimeData` (only if "restore clipboard
   after copying the selection" is enabled, enabled by default). Remember
   `GetClipboardSequenceNumber()`.
3. **Ctrl+C.** `SendInput` with the sequence Ctrl↓ C↓ C↑ Ctrl↑.
4. **Waiting for a change.** Every 10 ms compare `GetClipboardSequenceNumber()` with the remembered
   one, at most 400 ms. If it did not change, there is no selection, `cb("")`.
5. **Reading.** `CF_UNICODETEXT` is read directly through WinAPI, not through `QClipboard`: after the
   sequence number changes, Qt's cache may still hold the old data. The text is trimmed at the edges
   and limited in length (`translation/maxLength`, 20,000 characters by default; the cut does not
   split a surrogate pair).
6. **Restore.** Put the saved snapshot back. `ExcludeClipboardContentFromMonitorProcessing` and
   `CanIncludeInClipboardHistory = 0` formats are added to the restored data so the restore does not
   clutter the clipboard history (Win+V) and clipboard managers.
7. `cb(text)` exactly once.

Limitations (to be described in the help):

- **UIPI.** A normal process cannot send input to the window of a process running as administrator.
  Selection in such windows is not captured; `SendInput` silently does nothing.
- Programs where Ctrl+C means something else (terminals — interrupt), or where copying is disabled.
- The intermediate Ctrl+C is performed by the source program itself, without exclusion formats, so an
  entry may appear in the Win+V history. The formats in step 6 protect only the clipboard restore. The
  UI Automation strategy (§9.2) fully avoids this; until then the limitation is considered accepted.

Possible evolution: a strategy through **UI Automation** (UIA) that does not touch the clipboard.
Instead of `SendInput` Ctrl+C, the application gets the active element through `IUIAutomation`, finds
`TextPattern`, and reads the selection through `GetSelection()`/`GetText()`. The clipboard, Win+V
history, and the synthetic keypress are then not used.

UIA works only where the application implements TextPattern: modern browsers, Notepad, Office, many
WPF/UWP applications. Games, custom controls, some Java/Qt programs, and terminals do not expose text
this way, so the `grab()` interface does not change: internally a strategy chain "UIA → clipboard"
with the same fallback will appear.

UIPI remains a limitation for UIA too: a normal process cannot read the windows of a process running
as administrator. Bypassing it requires the `uiAccess` flag in the manifest, and that needs
application signing and installation into a protected directory such as `Program Files`; a per-user
installation in `%LOCALAPPDATA%\Programs` does not satisfy this. Therefore capture in elevated windows
is considered a separate limitation, not part of the basic UIA implementation.

### 4.5. Translation: types and service interface

`core/translationtypes.h`:

```cpp
struct TranslationRequest {
    QString text;
    QString sourceLang;   // internal code or "auto"
    QString targetLang;   // internal code
};

enum class TranslationError {
    None,
    Network,        // no connection, proxy, DNS, or TLS error
    Timeout,
    Auth,           // invalid/missing API key (HTTP 401/403)
    QuotaExceeded,  // DeepL 456, Google Cloud quota
    RateLimited,    // HTTP 429
    ServiceUnavailable, // blocked or temporarily unavailable web endpoint (HTML/403);
                        // gated free service while opt-in is off; not used by official APIs
    TooLong,        // request size limit exceeded
    UnsupportedLanguage,
    BadResponse,    // the response could not be parsed
    Cancelled,
    EmptyText,      // nothing to translate; determined before the request
};

struct TranslationResult {
    QString translatedText;
    QString detectedSourceLang;   // internal code if the service returned it
    QString serviceId;
    TranslationError error = TranslationError::None;
    QString errorMessage;         // technical details for the user/log, without the text itself
    bool ok() const;
};
```

The user-facing error text is assembled by the UI (`ui/translationerrortext`): a title by error kind
(translatable) and `errorMessage` in parentheses, for example "Network error (Host … not found)".

`core/translationservice.h`:

```cpp
class TranslationReply : public QObject {
    Q_OBJECT
public:
    // A ready reply for errors before the network: finished() will arrive through the event loop
    static TranslationReply *createFinished(const TranslationResult &r, QObject *parent = nullptr);
    const TranslationResult &result() const;
    bool isFinished() const;
    void abort();                 // → error = Cancelled, finished() is emitted once
signals:
    void finished();              // exactly once and never from inside translate()
protected:
    void finish(const TranslationResult &r);   // for implementations; repeated calls are ignored
    virtual void abortImpl();                  // stop network work
};

class TranslationService : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;            // "google", "deepl", "llm"
    virtual QString displayName() const = 0;
    virtual bool isConfigured() const = 0;     // DeepL without a key — false
    virtual QString configurationError() const { return {}; } // hint if not configured
    virtual QStringList sourceLanguages() const = 0;  // internal codes, without "auto"
    virtual QStringList targetLanguages() const = 0;
    // true for unofficial/free endpoints: the manager refuses them until the
    // user enables free web services in settings
    virtual bool requiresOptIn() const { return false; }
    virtual void applySettings(const AppSettings &s) = 0;   // keys, mode, variant
    // Returns the reply object; the caller owns it (deleteLater after finished)
    virtual TranslationReply *translate(const TranslationRequest &req) = 0;
};
```

- `abort()` first finishes the reply with `Cancelled` and then calls `abortImpl()`. Therefore an error
  reported by the aborted network request no longer changes anything.
- `finished()` is not emitted inside `translate()`, even if the reply is known immediately (unsupported
  language, empty text): the caller must have time to connect. `createFinished()` exists for this.
- The caller may delete the reply before `finished()` — this cancels the request without a signal.
- `applySettings()` applies keys, the Google mode, the DeepL English variant, and OpenAI-compatible
  provider settings without recreating the services.
- `configurationError()` returns a specific hint when `isConfigured() == false` (for example, "The
  provider needs a model."); `TranslationManager` shows it instead of the generic API key message.

The opt-in gate. A service that sends text to an unofficial or free endpoint overrides
`requiresOptIn()` to return `true`. `TranslationManager` refuses such a service while
`translation/allowFreeServices` is `false`: before any request it returns `ServiceUnavailable` with
the message "Enable free web services in Settings → Services." The check lives in the manager, not
in each service, so a new service cannot forget it. Google, DeepL (including its `free-web` mode),
and the OpenAI-compatible provider keep the default `false`; the DeepL `free-web` mode remains an
explicit mode with its own warning and is not gated. `AppController` hides gated services from the
main-window and tray pickers while the gate is off, and if the gate is turned off while a gated
service is active, it switches the active service to Google in memory and logs the switch. The
`translation/service` value in the INI is not rewritten, so enabling the gate again and restarting
restores the user's choice. `ServiceUnavailable` is also the error kind for a blocked or HTML
response from a free web endpoint; the official API paths do not use it.

The shared request queue. `QueuedWebService` (in `qtranslate_core`) serializes the replies of a
service that talks to one host: one instance per host, at most one active reply, and the next reply
starts no sooner than `rateLimitMs()` after the last network request. A reply reports every request
with `noteRequestSent()` right before handing it to `QNetworkAccessManager`, so the pause covers the
initial request, retries, and later chunks alike. A reply keeps its queue slot until it finishes; a
multi-chunk protocol therefore sends its chunks sequentially inside one slot and paces them with
`rateLimitMs()` instead of enqueuing every chunk as a separate reply. A reply that is finished while
queued is skipped, and a reply deleted while active is dropped through `QPointer`/`destroyed`. The
default pause is 2000 ms (the moved DeepL value); a service overrides it in its constructor, and
`setRateLimitMs(0)` disables the pause for tests only. The base knows no endpoints, service
settings, the opt-in gate, or UI; `DeepLWebService` is its first user.

Each service receives a `NetworkManager` pointer (the shared `QNetworkAccessManager` and
`makeRequest()` with a timeout and `User-Agent`) and does not create its own manager — this way the
proxy is configured in one place.

Response parsing is moved to pure functions (`googleparser`, `deeplparser`):
`QByteArray → TranslationResult`. This makes it possible to test them on saved responses without the
network.

### 4.6. `GoogleTranslateService`

Two modes, selected in settings:

**1. Free (no key, default).** The unofficial endpoint used by browser extensions:

```
POST https://translate.googleapis.com/translate_a/single?client=gtx&sl=<src|auto>&tl=<dst>&dt=t
Content-Type: application/x-www-form-urlencoded

q=<text>
```

POST instead of GET — to avoid hitting the URL length limit. The response is nested JSON arrays:
`data[0]` — a list of sentences `[translation, original, …]` (translations are concatenated),
`data[2]` — the detected source language code.

Risks: the endpoint is undocumented, may change, or return HTTP 429 on frequent requests. The parser
must be tolerant of extra/missing fields and return `BadResponse` on an unexpected format instead of
crashing.

What is known about the endpoint responses (old plan stage 2) and how
`GoogleParser::parseFreeResponse` accounts for it:

- rows in `data[0]` where the first element is not a string (transliteration `[null, null, "…"]` with
  `dt=rm`) are skipped;
- the language is taken from `data[2]`, or from `data[8][0][0]` if it is absent; the code is
  translated to the internal one taking synonyms into account: Hebrew arrives as `iw`, Chinese as
  `zh-CN` (§4.8). An unknown code — an empty `detectedSourceLang`, not an error;
- older versions returned "sparse" arrays (`[,,"en"]`, which is not JSON) — before re-parsing, `null`
  is inserted into the gaps;
- an unknown target language code does not produce an error: HTTP 200 with untranslated text arrives.
  Therefore codes are checked before the request: a language not in the table → `UnsupportedLanguage`
  immediately.

Free mode errors (`GoogleParser::errorForFreeHttpStatus` and the network layer):

| Situation | `TranslationError` |
|---|---|
| HTTP 429, 403 (the endpoint blocks frequent requests this way) | `RateLimited` |
| HTTP 413, 414 | `TooLong` |
| HTTP 5xx | `Network` (`errorMessage` = "HTTP 503") |
| other non-2xx | `BadResponse` |
| transfer timeout (`TimeoutError`/`OperationCanceledError` without our `abort()`) | `Timeout` |
| other `QNetworkReply` errors | `Network`, `errorMessage` = Qt error text |

The Qt error text contains the request URL. In free mode the URL contains neither text nor secrets; in
Cloud API mode (the key in `?key=`) this text must not be shown or written to the log as is — take
this into account in old plan stage 6.

For tests the endpoint address is replaced (`setEndpoint()`): `tst_googletranslateservice` starts a
local HTTP server.

**2. Google Cloud Translation API v2 (with a key).**

```
POST https://translation.googleapis.com/language/translate/v2?key=<KEY>
Content-Type: application/json

{"q": "…", "target": "ru", "source": "en", "format": "text"}
```

Response: `data.translations[].translatedText` and `detectedSourceLanguage`. `source` is not sent if
"auto" is selected.

Long text is split by `TextChunker` into chunks of up to 5000 characters (UTF-16) at
paragraph/sentence boundaries. A test POST of ~6900 characters to gtx passes, but the limit is
undocumented, so splitting is always done. The chunks are sent sequentially, and the result is
concatenated; on an error in any chunk the request finishes with that error. Spaces and newlines at
the edges of each chunk do not go into the request and are put back when concatenating — the service
may not preserve them, and at chunk boundaries this would break formatting. The detected language is
taken from the first chunk that has it.

Cloud API errors: 401/403 → `Auth`, 429 → `RateLimited`, 413 → `TooLong`, 5xx → `Network`, otherwise →
`BadResponse`. Google responds to an invalid key with code 400 and the text "API key not valid", so
`errorForCloudResponse(status, body)` checks the body and classifies such a 400 as `Auth`;
`quotaExceeded` and `rateLimitExceeded` in the body of a 400/403 produce the corresponding errors.
`errorMessage` contains only `HTTP <code>`, without the URL or key. For tests the address is replaced
by `setCloudEndpoint()`.

### 4.7. `DeepLService`

One `deepl` service with a mode in settings, like Google: `deepl.mode = api | free-web`.

- **API** — official DeepL, requires an API key. DeepL API Developer (formerly Free) has 1,000,000
  characters **in total**, with no monthly reset; after the limit, Growth/Pro is required. Without a
  key `isConfigured() == false`: an attempt to translate returns an `Auth` error with a hint to open
  settings.
- **Free web** — the unofficial web endpoint without a key, details in §4.7.1. It is switched in the
  same "DeepL" block; there is no separate service in the list. Old settings
  `translation/service=deepl-web` migrate to `service=deepl` + `deepl.mode=free-web`.

```
POST https://api-free.deepl.com/v2/translate     ← the key ends with ":fx"
POST https://api.deepl.com/v2/translate          ← otherwise
Authorization: DeepL-Auth-Key <KEY>
Content-Type: application/json

{"text": ["…"], "target_lang": "RU", "source_lang": "EN"}
```

- The host is chosen automatically by the `:fx` key suffix.
- Authorization by header only; passing the key in the body/URL is deprecated at DeepL.
- `source_lang` is not sent with "auto"; the target `en` is replaced by the variant from settings
  (`EN-US`/`EN-GB`).
- Response: `translations[].text`, `translations[].detected_source_language`.
- Error codes: 403 → `Auth`, 456 → `QuotaExceeded`, 429 → `RateLimited`, 413 → `TooLong`, 5xx →
  `Network`; `errorMessage` contains only `HTTP <code>`.
- The request body limit is 128 KiB; long text is split by `TextChunker` and sent in **one** request
  as a `text` array. Spaces at the edges of chunks are not sent and are restored when concatenating
  the response in order.
- In settings the **Check key** button calls `GET /v2/usage` with the key from the input field and
  shows `character_count`/`character_limit` or a clear error. Check errors do not contain the key.

### 4.7.1. The `free-web` mode — the free DeepL web endpoint

The internal `DeepLWebService` (with service id `deepl`) is used by the `DeepLService` facade when the
`free-web` mode is selected. No key is needed; settings show a warning that the endpoint is
unofficial. The service list keeps a single "DeepL" item.

```
POST https://oneshot-free.www.deepl.com/v1/translate
Authorization: None
Accept: application/json
Content-Type: application/json

{"text": ["…"], "source_lang": "de", "target_lang": "pt-BR"}
```

- `source_lang` is not sent with "auto".
- DeepL codes are converted to the web endpoint form: base languages in lowercase (`DE` → `de`),
  regional ones — `pt-BR`, `en-US`, `zh-Hans`. The endpoint treats an uppercase base code (`DE`) as
  unknown and silently returns the source text, so the conversion is mandatory.
- Response: `translations[].text`, `translations[].detected_source_language`; the format matches the
  official API, and parsing reuses `DeepLParser`.
- The chunk limit is 5000 characters. Chunks go in one request as a `text` array; edge spaces are not
  sent and are restored when concatenating.
- Requests to the host are serialized by the shared `QueuedWebService` queue with the unchanged 2 s
  pause, so they go no more often than once every 2 s, including requests from the main window, the
  popup, and selection replacement. On `429` — a 3 s pause and one retry; the retry reports its
  request to the queue as well. `403` is treated as a block/limit (`RateLimited`), `413` → `TooLong`,
  `5xx` → `Network`.
- `isConfigured() == true`; no key or cookies are used. The text and response body are not written to
  the log.
- Tests — `tst_deeplwebparser`, `tst_deeplwebservice`, `tst_queuedwebservice` (the shared queue with
  a fake service), and the `free-web` mode in `tst_deeplservice`;
  live requests are only run as a manual smoke because the endpoint is unstable and rate-limited.
  Switching the mode in the UI: `deeplMode` in `SettingsDialog`, the API key field is disabled in
  `free-web`.

### 4.7.2. `OpenAiCompatibleService` — translation via an OpenAI-compatible API

The `llm` service with a configurable name: for `openrouter.ai` it shows **OpenRouter**, for
`api.openai.com` — **OpenAI**, for a local address — **Local LLM**, for other hosts — the host itself.
This is one adapter, not a separate class per provider: OpenRouter, OpenAI, TeamRouter, Ollama,
LM Studio, llama.cpp, and other `POST /chat/completions` implementations use one protocol.

```
POST <baseUrl>/chat/completions
Authorization: Bearer <KEY>          ← not sent for a local address without a key
Content-Type: application/json
HTTP-Referer: https://github.com/youkaicat/QTranslate
X-Title: qtranslate
```

- `baseUrl` defaults to `https://openrouter.ai/api/v1`. `OpenAiChat::chatCompletionsUrl()` accepts both
  the full `.../v1/chat/completions` and a base without `/v1` (for Ollama/llama.cpp). OpenRouter offers
  free models (`openrouter/free`), but limits and quality depend on the model.
- The model is set as a string; for OpenRouter the default is `openrouter/free`, the **Load models**
  button checks the key via `GET /key` (OpenRouter only) and fills the list from `GET /models`. For a
  local server the key is optional, and `/key` is not requested.
- Request: `messages` from a system and a user message, `temperature=0.2`, `stream=false`; the user
  text is passed as data, not as instructions.
- With `auto` the model is asked to return JSON `{"detected_language":"en","translation":"..."}`; with
  an explicit source language detected is taken from the request. Markdown wrappers and plain text are
  supported as a fallback, so weak models do not break translation.
- Spaces at the edges of the source text are preserved; the text itself and the response are not
  written to the log.
- Errors: 401/403 → `Auth`, 402 → `QuotaExceeded`, 429 → `RateLimited`, 413 → `TooLong`, 5xx →
  `Network`, 4xx → `BadResponse`; `finish_reason=length` → `TooLong`.
- In `AppSettings` the `[llm]` group: `baseUrl`, `apiKey` (DPAPI), `model`. When the base URL or model
  changes, the `TranslationManager` cache is cleared.
- Requests go through the shared `NetworkManager`, so the system/manual HTTP/SOCKS5 proxy and timeout
  apply as for the other services. The **Load models** button uses a temporary manager with the
  proxy/timeout from the dialog fields that have not been saved yet.
- The `src/ai/openaichat.*` transport does not depend on `TranslationService`: future AI actions and
  the dictionary will use it too.

### 4.8. Languages — `languages`

The program uses its own language codes internally (`en`, `ru`, `de`, `pt-BR`, `zh-Hans`…).
Services map them to their own codes:

| Internal | Google | DeepL source | DeepL target |
|---|---|---|---|
| `en` | `en` | `EN` | `EN-US` (configurable: `EN-GB`) |
| `pt-BR` | `pt` | `PT` | `PT-BR` |
| `zh-Hans` | `zh-CN` | `ZH` | `ZH-HANS` |
| `ru` | `ru` | `RU` | `RU` |
| `he` | `iw` | `HE` | `HE` |
| `nb` | `no` | `NB` | `NB` |
| `fil` | `tl` | — | — |

The module stores a table `{code, English name, native name, service codes}`
(about 110 Google languages, of which about 35 are available in DeepL) and provides functions in the
`Languages` namespace:

- `toServiceCode(serviceId, code, direction)` — empty string if the service does not support the language;
- `fromServiceCode(serviceId, serviceCode, direction)` — case-insensitive; first the code for the requested
  direction, then the other one (DeepL `EN-US` as source, `PT` as target), then synonyms
  (Google `he`/`jv`/`zh`, DeepL `EN-GB`); unknown code — empty string. Reverse
  conversion is needed for `detectedSourceLang`;
- `supported(serviceId, direction)` — language lists for services and `LanguageComboBox`;
- `closest(serviceId, code, direction)` — nearest supported code: exact, then a language of the same
  base (`pt-BR`/`pt-PT`, `zh-Hans`/`zh-Hant`), system, `en`, first available;
- `displayName(code)` — UI name, marked with `QT_TRANSLATE_NOOP` (context `Languages`) for
  localization in old plan stage 8; `englishName(code)` — stable English name for LLM prompts, not
  dependent on the UI language; `nativeName(code)` — native name (tooltip in the list);
- `autoCode()` — `"auto"`; `forLocale(QLocale)` / `systemLanguage()` — system language considering
  script and region (`zh-Hans`/`zh-Hant`, `pt-BR`/`pt-PT`, Nynorsk → `nb`), otherwise `en`.
- The `llm` service uses internal language codes and the full Google catalogue: LLM does not require
  a separate code table, and `englishName()` gives the model an unambiguous language name.

Service language lists differ (DeepL has fewer). `LanguageComboBox` shows only
the languages supported by the selected service; when the service changes, `AppController` replaces the current language via
`Languages::closest` (for example, `pt` → `pt-BR`, `fil` → system/`en`). "Auto-detect" always remains
the source language.

### 4.9. `TranslationManager`

A facade over the services that `AppController` and the UI work with.

```cpp
class TranslationManager : public QObject {
    Q_OBJECT
public:
    enum class Channel { MainWindow, Popup, InPlace };

    void registerService(TranslationService *service);   // takes ownership; the first is active
    QList<TranslationService *> services() const;
    TranslationService *service(const QString &id) const;
    TranslationService *activeService() const;
    void setActiveService(const QString &id);
    void setMaxLength(int length);                         // 20,000 by default; from settings — old plan stage 5
    bool freeServicesAllowed() const;                      // translation/allowFreeServices
    void setFreeServicesAllowed(bool allowed);             // gated services are refused while false

    // channel — the request "channel": Popup, MainWindow, or InPlace.
    // A new request in the same channel cancels the previous incomplete one.
    TranslationReply *translate(const TranslationRequest &req, Channel channel);
    void cancel(Channel channel);
    void cancelAll();                                      // on program exit (§5.6)
signals:
    void activeServiceChanged(const QString &id);
};
```

The manager keeps only a `QPointer` to the current reply of each channel: the caller owns the reply.
`translate()` first checks the cache, then wraps the service reply in a `Reply` (caching and
retry to the additional language); the manager's interface does not change. A cancelled reply
arrives at the caller with `Cancelled`, and the UI does not show it.

Responsibilities:

1. **Cancelling stale requests.** The user pressed Ctrl+Q, did not wait, and selected other text —
   the first request is cancelled, its result will not appear on top of the new one. Channels are independent:
   a translation in the main window does not cancel the popup.
2. **Smart choice of the translation language.** There are two languages in settings: the primary (for example, `ru`) and
   the additional (`en`). If the source language is "auto" and the service determines that the text is already in the primary
   language, the request is automatically repeated with translation to the additional language. So one key
   works both ways: English → Russian and Russian → English.
3. **Cache.** LRU of 200 entries, key — `serviceId|src|dst|text`. Re-translating the same
   fragment is instant and does not consume DeepL quota. The cache is in memory only.
4. **Checks before the network.** Empty text (`EmptyText`), exceeding `maxLength` (`TooLong`),
   a service whose `requiresOptIn()` is true while `freeServicesAllowed()` is false
   (`ServiceUnavailable`, with a message pointing to Settings → Services), an unconfigured service
   (`Auth`) — the error is returned immediately, without a request (via
   `TranslationReply::createFinished`, that is, asynchronously). `AppController` keeps
   `setFreeServicesAllowed()` in sync with `translation/allowFreeServices` and filters gated
   services from the pickers.

### 4.10. Network and proxy — `NetworkManager`

Owns the application's single `QNetworkAccessManager`.

```cpp
enum class ProxyMode { None, System, Manual };
enum class ProxyType { Http, Socks5 };

struct ProxySettings {
    ProxyMode mode = ProxyMode::System;
    ProxyType type = ProxyType::Http;
    QString host;
    quint16 port = 0;
    bool authEnabled = false;
    QString user;
    QString password;
};

class NetworkManager : public QObject {
    Q_OBJECT
public:
    QNetworkAccessManager *nam();
    void applyProxy(const ProxySettings &p);
    const ProxySettings &proxySettings() const;
    QNetworkRequest makeRequest(const QUrl &url) const;   // timeout, User-Agent, HTTP/1.1
    // Proxy test without saving: a temporary QNAM with the given parameters
    void testProxy(const ProxySettings &p, const QUrl &url,
                   std::function<void(bool ok, const QString &message)> cb);
    QString describeNetworkError(QNetworkReply *reply, bool includeDetails = true) const;
};
```

Mapping to Qt:

| Mode / type | Implementation |
|---|---|
| No proxy | `QNetworkProxy::NoProxy` |
| System | `QNetworkProxyFactory::setUseSystemConfiguration(true)` (WinINet/WPAD, PAC) |
| HTTP | `QNetworkProxy::HttpProxy`; for https addresses Qt creates the `CONNECT` tunnel itself |
| SOCKS5 | `QNetworkProxy::Socks5Proxy`; DNS names are resolved on the proxy side |

Login and password are set directly in `QNetworkProxy`. `proxyAuthenticationRequired` does not show a
dialog: if credentials are present, they are sent once; a repeated request is rejected. Proxy
errors (`ProxyAuthenticationRequiredError`, `ProxyConnectionRefusedError`, `ProxyNotFoundError`,
`ProxyTimeoutError`, etc.) are converted into short "Proxy authentication failed" /
"Proxy connection failed" messages via `describeNetworkError`; other errors keep the Qt text. For
Cloud URLs with a key, `includeDetails = false` leaves only the generic "Network error".

After `applyProxy`, `nam->clearConnectionCache()` is called so that already open keep-alive
connections do not continue bypassing the new proxy. `testProxy` uses the same temporary QNAM,
the same timeout/User-Agent, and considers any HTTP response a success (except `407 Proxy Authentication
Required`); in the UI, the test goes to `https://www.gstatic.com/generate_204`. Previously,
`https://translate.googleapis.com/` was used, which responds with `404`, and the test incorrectly considered a working
proxy to be faulty.

**On "HTTPS proxies" (decision made).** An HTTPS proxy in the project means an HTTP proxy
through which https requests go (a `CONNECT` tunnel). There is no separate type for this: the `Http`
type serves both http and https addresses. A proxy with a TLS connection *to the proxy itself* is not supported by
`QNetworkProxy`, and the project does not need it.

Qt does not support SOCKS4; only SOCKS5 is offered in the UI.

Common request parameters: `setTransferTimeout(10 000 ms)` (configurable), a persistent
`User-Agent: qtranslate/<version>`, HTTPS only.

**HTTP/1.1 for free Google.** Qt 6.12 negotiates HTTP/2 by default, and
`translate.googleapis.com/translate_a/single` in our tests consistently responded to such requests with
`HTTP 429`, although the same GET/POST over HTTP/1.1 (curl, .NET) passed. Therefore `makeRequest()` sets
`QNetworkRequest::Http2AllowedAttribute = false`; after this, gtx translates without 429. Requests to all
services go over HTTP/1.1 — this does not matter for DeepL, and the behavior of free Google
becomes predictable. The regression is checked in `tst_networkmanager::requestsUseHttp11`.

**State at old plan stage 8.** `applyProxy`, `testProxy`, proxy authorization/error handling, and
`describeNetworkError` are implemented. Modes `None`/`System`/`Manual`, types HTTP/SOCKS5; after changing the
proxy, the connection cache is reset. The proxy is applied at startup and when settings are saved.
The system mode and manual HTTP proxy were tested on a real proxy `127.0.0.1:2080`.

### 4.11. Settings — `AppSettings` and `SettingsStore`

`AppSettings` is a plain value structure with all parameters (nested groups `general`,
`hotkeys`, `translation`, `google`, `deepl`, `llm`, `network`, `popup`, `mainWindow`, and
`settingsVersion`). `SettingsStore` loads and saves it and reports changes:

```cpp
class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(QObject *parent = nullptr);
    SettingsStore(const QString &filePath, const SecretCodec &codec,
                  QObject *parent = nullptr);
    static QString defaultFilePath(const QString &applicationDir,
                                   const QString &appDataDir);
    const AppSettings &current() const;
    void setSecretCodec(const SecretCodec &codec);
    void load();
    void save(const AppSettings &s);       // writes to disk and emits changed
signals:
    void changed(const AppSettings &oldS, const AppSettings &newS);
};
```

The settings window edits a **copy** of `AppSettings`; on "OK" it passes it to `save()`.
`AppController` compares the old and new values and applies only what changed
(re-registers hotkeys, changes languages, autostart, etc.).

Storage: an INI file via `QSettings`.

- Normal mode: `%APPDATA%\qtranslate\qtranslate.ini`.
- Portable mode: if `qtranslate.ini` is next to the exe, it is used. The path is chosen by the
  pure `SettingsStore::defaultFilePath(applicationDir, appDataDir)`, so the rule is tested
  without writing next to the test exe.

`organizationName` is not set (§4.1), so the default `QSettings` is not suitable for this.
`SettingsStore` opens the INI by an explicit path — `QSettings(path, QSettings::IniFormat)`. The
`general` group in the INI is called `[app]`: `QSettings` has reserved the name `[General]` for top-level
keys and otherwise escapes the group as `[%General]`.

Secrets (API keys, proxy password) are stored encrypted via Windows DPAPI
(`CryptProtectData`, tied to the user account) in base64. The
`platform/secretprotector` module provides `protect(QString) → QByteArray` and
`unprotect(QByteArray) → QString`. `SettingsStore` does not know WinAPI: the application passes it a
`SecretCodec` of two `std::function`s; without a codec, secrets are not written at all (in plain text —
never). If decryption fails (the file was moved to another machine), the field is considered empty.

File keys:

| Key | Type | Default |
|---|---|---|
| `app/autostart` | bool | `false` |
| `app/startMinimized` | bool | `true` |
| `app/uiLanguage` | string | `system` (`system`, `en`, `ru`, `es`, `de`, `fr`, `pt-BR`, `it`, `pl`, `uk`, `tr`, `zh-Hans`, `ja`, `ko`, `ar`, `hi`) |
| `app/restoreClipboard` | bool | `true` |
| `hotkeys/popup` | QKeySequence (portable text) | `Ctrl+Q` |
| `hotkeys/replaceSelection` | QKeySequence | `Ctrl+Shift+Q` |
| `hotkeys/mainWindow` | QKeySequence | `Ctrl+W` |
| `hotkeys/showMainWindow` | QKeySequence | empty |
| `hotkeys/pauseInFullscreen` | bool | `true` |
| `translation/service` | string | `google` |
| `translation/sourceLang` | string | `auto` |
| `translation/primaryLang` | string | system language |
| `translation/secondaryLang` | string | `en` (or `ru` if the system language is English) |
| `translation/maxLength` | int | `20000` |
| `translation/autoTranslateDelayMs` | int | `700` (0 — off) |
| `translation/allowFreeServices` | bool | `false`; enables services whose `requiresOptIn()` is true |
| `google/mode` | `free` \| `cloud` | `free` |
| `google/apiKey` | secret | empty |
| `deepl/mode` | `api` \| `free-web` | `api`; the old `translation/service=deepl-web` migrates to `deepl` + `free-web` |
| `deepl/apiKey` | secret | empty |
| `deepl/englishVariant` | `EN-US` \| `EN-GB` | `EN-US` |
| `llm/baseUrl` | string | `https://openrouter.ai/api/v1` |
| `llm/apiKey` | secret | empty |
| `llm/model` | string | `openrouter/free` |
| `network/proxyMode` | `none` \| `system` \| `manual` | `system` |
| `network/proxyType` | `http` \| `socks5` | `http` |
| `network/proxyHost`, `proxyPort` | string, int | — |
| `network/proxyAuth`, `proxyUser` | bool, string | — |
| `network/proxyPassword` | secret | — |
| `network/timeoutMs` | int | `10000` |
| `popup/width`, `popup/height`, `popup/autoCloseOnFocusLoss` | — | `460`, `260`, `true` |
| `mainWindow/geometry`, `mainWindow/state` | QByteArray | — |

`settingsVersion` is written to the file; when the format changes, migration is performed in `SettingsStore::load()`.

### 4.12. Autostart — `Autostart`

```cpp
namespace Autostart {
    QString commandForExecutable(const QString &path);
    bool isEnabled();
    bool setEnabled(bool on);   // false — no access to the registry
    bool sync(bool wanted);     // bring the registry in line with the setting, update the path
    bool isApproved();          // takes StartupApproved into account
}
```

- Entry in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `qtranslate` =
  `"C:\…\qtranslate.exe" --minimized` (via `QSettings` with `NativeFormat`). Administrator rights
  are not required.
- `sync()` is called on every start: if autostart is enabled and the exe path has changed
  (the program was moved or updated to a different folder), the entry is updated.
- The user can disable autostart in Task Manager — then Windows sets a flag in
  `…\Explorer\StartupApproved\Run`. We do not overwrite it, but only show "disabled in Task Manager"
  in settings.

### 4.13. User interface

#### `TrayIcon`

`QSystemTrayIcon` with a menu:

```
Show window
Service ▸  ● Google Translate
           ○ DeepL
───────────
Settings…
Quit
```

- Left click — show/hide the main window.
- Icon tooltip: "qtranslate — Ctrl+Q / Ctrl+W" (the current shortcuts).
- `showMessage()` for notifications: hotkey in use, network error when the window is hidden,
  first launch.
- If `QSystemTrayIcon::isSystemTrayAvailable()` returns `false`, the main window is always shown
  at startup, and closing the window exits the program.

#### `PopupWindow` (Ctrl+Q)

```
┌──────────────────────────────────────────────┐
│ EN → RU ▾                    Google   📌  ✕   │
├──────────────────────────────────────────────┤
│ Translation of text that can be selected     │
│ and copied…                                  │
├──────────────────────────────────────────────┤
│ ⧉ Copy   ⇄ Open in Main Window               │
└──────────────────────────────────────────────┘
```

- Flags: `Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint` — no taskbar button.
- Created lazily on the first Ctrl+Q and reused to appear without delay.
- Position: at the cursor when the key is pressed (the point is remembered before the selection capture), with
  clamping to `QScreen::availableGeometry()` of the monitor where the cursor is. The pure geometry
  is moved to `core/popupplacement`: the window is first placed bottom-right, reflected at the edge
  left/up, then pressed against the screen.
- Width 460 logical pixels (the setting was added in old plan stage 5). Height adapts to the result text
  up to `min(600, 70% of the work area)`; then scrolling.
- States: "loading" (animated spinner, shown immediately after the selection capture), "result",
  "error" (text and "Retry" button), "no selection" (short hint, closes after
  1.5 s).
- Closing: Esc and ✕ always; focus loss — if not pinned 📌. Pinning applies within
  one invocation and resets on a new Ctrl+Q. The language menu is a temporary popup: while the application
  remains active and another of our windows has not become active, closing the menu does not close the window; a click in
  another program or in the main window closes it. The `popup/autoCloseOnFocusLoss` setting was added in old plan stage 5;
  the behaviour is enabled by default.
- Clicking "Auto → Russian ▾" opens the target language menu; the selection immediately re-translates the saved
  source text. After the result, the detected language is shown in the line.
- "Copy" puts the translation on the clipboard. "Open in Main Window" emits
  `openInMainWindowRequested(request, result)`; `AppController` passes both texts to
  `MainWindow::showTranslation()` without a new request and shows the main window.
- Hiding emits `closed()`; `AppController` cancels the incomplete request of the `Popup` channel so that
  a late response does not appear on top of the next one. A new Ctrl+Q replaces the old request of the same channel.
- Activation: Windows allows the process that received `WM_HOTKEY` to bring the window to the foreground,
  so `activateWindow()` after the hotkey works.

#### `MainWindow` (Ctrl+W)

```
┌───────────────────────────────────────────────────────────┐
│ [Auto ▾]  ⇄  [Russian ▾]          Service: [Google ▾]       │
├─────────────────────────────┬─────────────────────────────┤
│ Source text                 │ Translation (read-only)     │
│                             │                             │
├─────────────────────────────┴─────────────────────────────┤
│ Detected language: English   [Translate Ctrl+Enter] [Copy]│
└───────────────────────────────────────────────────────────┘
```

- Two `QPlainTextEdit`s: source text and translation (read-only).
- Auto-translate while typing with a delay `autoTranslateDelayMs` (0 — off); "Translate" and Ctrl+Enter —
  translate immediately. Pressing again before the response replaces the request (`MainWindow` channel, §4.9).
- ⇄ swaps the languages, moves the translation into the source text field and translates it immediately. If
  the source language is "auto", the new target becomes the detected language of the last translation; while it is
  unknown, the button is disabled.
- Status line: "Translating…", the detected language (when the source is "auto"), errors (in red,
  text from `ui/translationerrortext`). "Copy" is available when there is a translation.
- The service choice shows the active service and is available only when there is more than one service (old plan stage 6).
- The window does not know about services and the network: it emits `translateRequested(request)`, and `AppController`
  calls `TranslationManager` and returns the result via `showTranslationStarted()` /
  `showResult(result)`; the result is shown only if it is the current request of the window.
- `translateText(text)` inserts the text received by the Ctrl+W scenario into the source text field,
  places the cursor at the beginning and starts the translation.
- `showTranslation(request, result)` shows the translation passed from the popup: it sets the
  languages, both texts and the status, but does not start a new request.
- `closeEvent` hides the window to the tray; exit is only from the tray menu.
- Geometry is saved in settings.

#### `LanguageComboBox`

A `QComboBox` with internal language codes: `setLanguages(codes, includeAuto)` (sorted by
name, "Auto-detect" on top, the current language is kept if it remains in the list),
`currentCode()` / `setCurrentCode()`, signal `currentCodeChanged`. The nearest language substitution
when the service changes (§4.8) is added in old plan stage 6.

#### `SettingsDialog`

The modal dialog edits a copy of `AppSettings`; "OK" returns it, `AppController` saves and
applies the changes, "Cancel" changes nothing. Tabs:

1. **General** — autostart (with a hint if Windows disabled it in Task Manager), start
   minimized, UI language (system/English/Русский), clipboard restoration, closing the
   popup on focus loss.
2. **Hotkeys** — `QKeySequenceEdit` for Ctrl+Q, Ctrl+Shift+Q (translation with selection
   replacement), Ctrl+W, and showing the main window. An empty field disables the action. Before accepting,
   single-key composition, Win32 support, and the absence of duplicates are checked. While focus is in any field,
   the dialog emits `hotkeyEditingChanged(true)`, and `AppController` suspends global
   hotkeys; after focus leaves/closes — resumes them.
3. **Translation** — source language (with "Auto-detect"), primary and additional languages, maximum
   length, auto-translate delay. The additional language is used for smart switching
   when the text is already in the primary language (old plan stage 8).
4. **Services** — Google: mode (free/Cloud API), key (password echo). DeepL: mode
   (API with a key or a free web endpoint without a key), key (password echo), English variant,
   "Check key" button with an asynchronous result (`character_count`/`character_limit` or
   an error). In `free-web` mode, the key field and the check are disabled, and a warning about
   being unofficial is shown; there is no separate "DeepL (free web)" group. The check button does not block the dialog;
   the key is taken from the input field. Keys have been stored via DPAPI since old plan stage 5.
   At the end of the page, the "Free web services" group has the `allowFreeServices` checkbox and
   the `freeWebHint` warning: free no-key services are mostly unofficial, may stop working or block
   requests, and send the text to a third party; the switch is off by default.
5. **Network** — mode (no proxy/system/manual), type HTTP/SOCKS5, host, port, authentication,
   login, password (password echo), timeout, "Test connection" button with an asynchronous result.
   Fields are enabled only for manual mode; the dialog emits `proxyTestRequested()`, and
   `AppController` tests the not-yet-saved parameters via `NetworkManager::testProxy`.

#### `AppIcon`

Icons for the tray and the window. For the tray — separate variants for the light and dark taskbar theme
(`QStyleHints::colorScheme()`); during a request, an animated indicator can be shown.
The exe icon (`resources/icons/app.ico`) and the version are embedded via `resources/qtranslate.rc`.

#### Localization

Strings are marked with `tr()`/`QT_TRANSLATE_NOOP`. English is the source language; `i18n/` contains
`qtranslate_ru_RU.ts` and translations into Spanish, German, French, Portuguese (Brazil),
Italian, Polish, Ukrainian, Turkish, Chinese (Simplified), Japanese, Korean, Arabic,
and Hindi. All `.ts` files are compiled by `qt_add_translations` and embedded into `:/i18n` resources.

`core/uilanguages` stores the supported codes, native names, and the path to `.qm`. `normalize()`
resolves `system`, regional tags (`de-DE`, `ru_RU`), and special cases: `pt-PT` → `pt-BR`,
`zh-CN`/`zh-Hans` → `zh-Hans`; unsupported languages and `zh-Hant` still fall back to English.
`AppController::applyUiLanguage()` loads the required `QTranslator`, enables `Qt::RightToLeft` for Arabic,
and broadcasts `LanguageChange`; the main window reapplies static texts, the tray
rebuilds the menu, and the popup is recreated on the next call.

`tst_uilanguages` checks code normalization and the language table, `tst_translations` — the completeness of all
`.ts` files, preservation of `%1`/`%2`, emoji, and edge whitespace. Translations are a machine-assisted first
pass; before a public release, native-speaker review is desirable, especially for Arabic RTL and
long strings in settings.

Standard Qt dialogs are translated by separate `qt_<locale>.qm` files; `AppController::applyUiLanguage()`
loads them from `../translations` next to the exe (in a dev build — from
`QLibraryInfo::TranslationsPath`). `windeployqt --translations` deploys directories for
`ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar`; Qt 6.12 has no `qt_hi.qm`, so standard
buttons and menus for Hindi remain English.

### 4.14. Portability and the Linux support plan

Windows code is isolated: `platform/*.h` headers do not include `<windows.h>`, and the implementations are
`*_win.cpp`; the Linux backends are `*_linux.cpp`, and `*_stub.cpp` remains only for platforms
without a backend. `qtranslate_core`, `net/`, `services/`, and the UI do not use WinAPI. The Linux
tree configures, builds, and links: 29 tests (21 shared plus 8 Linux platform tests) pass under
`xvfb-run` on Debian 13 (trixie, Qt 6.8.2, GCC 14.2) and Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2;
Clang 16 is also clean), with `-Wall -Wextra` and no warnings. The project minimum stays Qt 6.5+.
X11 is the full Linux target; Wayland is limited, as the table and the notes below show.

| Module | Windows (current) | Linux X11 (0.12.0) | Linux Wayland (0.12.0, limited) |
|---|---|---|---|
| Hotkeys | `RegisterHotKey` | `XGrabKey` via xcb/Xlib | the `org.freedesktop.portal.GlobalShortcuts` portal; if unavailable — CLI commands bound in the DE |
| Selection capture | `SendInput` Ctrl+C | XTest + `QClipboard` | synthetic input is unavailable; the user copies manually |
| Translation insertion | `SendInput` Ctrl+V | XTest Ctrl+V | not reliably available; the translation is put on the clipboard with a message (no synthetic input) |
| Active window | `GetForegroundWindow` | `_NET_ACTIVE_WINDOW`/`XGetInputFocus` | no reliable API; "in-field" mode is not guaranteed |
| Autostart | `Run` registry | `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` | same |
| Secrets | DPAPI | libsecret | libsecret; fallback — a 0600 file + warning |
| Tray | `QSystemTrayIcon`/WinAPI | StatusNotifierItem | StatusNotifierItem; GNOME needs the AppIndicator extension |
| Settings | `%APPDATA%\qtranslate` | `$XDG_CONFIG_HOME/qtranslate` | same |
| Logs | `%LOCALAPPDATA%\qtranslate` | `$XDG_STATE_HOME/qtranslate` | same |

**CLI commands.** `--translate-popup`, `--translate-main`, and `--translate-inplace` are parsed by
`src/app/commandline.*` and forwarded over the `SingleInstance` socket to a running instance (or
start one). They are mutually exclusive, combine with `--minimized`/`--verbose`, and are the
portable trigger for desktop shortcuts and Wayland; on X11 they remain a convenient fallback and
are covered by `tst_cli`.

**Tray.** Qt uses StatusNotifierItem/DBus. In GNOME, application icons are not shown by default, so
the AppIndicator extension is required; if the tray is unavailable, the application shows the main
window and warns instead of exiting silently. The `--translate-main` path avoids the no-tray
warning.

**Secrets.** libsecret (`secret_password_store_sync`) is used when it is found at build time and
`org.freedesktop.secrets` is available; tokens are `l:<uuid>`. Otherwise secrets are written to a
`0600` file under `$XDG_CONFIG_HOME/qtranslate/secrets` with a `0700` directory (`f:<uuid>`), and a
warning is shown in settings. A capability drop during save shows a message box; an unchanged `l:`
token is reused through a transient keyring outage, and `f:` tokens migrate to libsecret on the next
save. Silently storing passwords in plain text is not acceptable.

**Build and packages.** CMake installs `bin/qtranslate`, the freedesktop desktop file, and the
256x256 hicolor icon; `packaging/linux/package.sh` owns the bundle and builds the trixie
`tar.gz`/`.deb`/AppImage and `SHA256SUMS-linux.txt` in the `debian:trixie` container (KDE, GNOME, and
the shell read the `.desktop` file; `Icon=qtranslate` matches the installed icon). `--target astra`
builds the native Astra `-astra.tar.gz` and `+astra1` `.deb` with Astra's Qt 6.10.2; those are local
verification artifacts, not CI-published. CI publishes only trixie artifacts. The Windows platform
tests (`tst_hotkeymanager`, Windows variants of `tst_autostart`/`tst_secretprotector`) have Linux
analogues; the other 20 tests are shared.

**Wayland limitations.** Global shortcuts and synthetic input are not base capabilities of
Wayland. The `GlobalShortcuts` portal is not supported by all desktops and versions; insertion into
the active field and Ctrl+Shift+Q on Wayland cannot be guaranteed without external tools or the
RemoteDesktop portal. Therefore X11 is the full target, and Wayland is limited: the portal when
available, Ctrl+Q with manual copying, the CLI commands, in-place feedback that writes the
translation to the clipboard, and explicit warnings in the interface. No `ydotool`/`wtype` or other
synthetic-input backend is used. macOS is not planned.

### 4.15. Spelling, grammar, and punctuation checking (plan)

A future task: a separate text-checking mode not related to translation. The full design,
providers, and criteria are in [`proofreading.md`](proofreading.md).

- A separate `ProofreadingService` and `ProofreadingIssue` with `offset`/`length` in UTF-16,
  `message`, `replacements`, `ruleId`, `category`, `severity`.
- Providers: LanguageTool (HTTP/self-hosted, grammar and punctuation), Hunspell/Nuspell
  (spelling only), Yandex Speller (spelling ru/uk/en), LLM (optional, via diff).
- A separate "Text checking" settings tab, independent `proofreading/*` keys; API keys —
  DPAPI. `checkBeforeTranslation=false` and `autoReplace=false` by default.
- Replacement in the main window — via `QTextDocument` with undo; in an external application — via the
  Ctrl+Shift+Q mechanism with active-window checking and confirmation.
- Text is not logged; public APIs have limits; for regular use, a dedicated
  LanguageTool server.

### 4.16. LLM actions and the dictionary (partially implemented)

Translation via an OpenAI-compatible provider is already implemented (§4.7.2): the `llm` service, `[llm]`
settings, and the `src/ai/openaichat.*` transport. The other actions are a future task; the full design,
providers, and criteria are in [`llm-actions.md`](llm-actions.md).

- `LlmProvider` — the same OpenAI-compatible transport for the cloud (OpenAI, OpenRouter,
  Gemini-compatible) and local servers (Ollama, LM Studio, llama.cpp).
- `AiAction`/`AiActionRegistry` — actions `translate`, `proofread`, `summarize`, `rewrite`,
  `explain`, `pronounce`; prompts with a fixed role, low temperature, and validation.
  `translate` already works as a regular `TranslationService`; the others will appear in a separate
  panel with a preview.
- The dictionary is a separate `DictionaryService`, not a `TranslationService`; sources: LLM (JSON schema),
  Google `dt=bd` for short texts, a local CSV glossary, Wiktionary. The design is in
  [`dictionary.md`](dictionary.md).
- Settings — a separate "AI" tab: base URL, model, key (DPAPI), timeout, connection test;
  `llm/*` keys. The dictionary uses the same `[llm]` settings but has its own
  cache and does not affect translation.
- The result is shown in a preview/diff; text replacement — only on confirmation.
- LLM does not replace DeepL/Google/LanguageTool by default; ASR/TTS/OCR are separate future tasks.

---

## 5. Main scenarios

### 5.1. Startup

```
main
 ├─ QApplication, setQuitOnLastWindowClosed(false)
 ├─ SingleInstance: already running? → send "activate" → exit(0)
 ├─ SettingsStore: DPAPI SecretCodec, load()   (migration, secret decryption)
 ├─ QTranslator for general/uiLanguage
 └─ AppController
     ├─ NetworkManager (timeout, applyProxy), GoogleTranslateService + DeepLService → TranslationManager
     ├─ languages/service/maxLength/restoreClipboard from settings
     ├─ MainWindow + geometry restoration; PopupWindow — lazily on the first Ctrl+Q
     ├─ TrayIcon.show()
     ├─ HotkeyManager.register(...)  → occupied combinations → tray notification
     ├─ Autostart::sync(settings.autostart)
     └─ if there is no --minimized and startMinimized is off → MainWindow.show()
```

Everything that is not needed for the tray icon to appear is created lazily (settings window, popup window).

### 5.2. Ctrl+Q — translation in the popup window

```
User              HotkeyMgr     AppController     SelectionGrabber   TranslationMgr   PopupWindow
    │ Ctrl+Q          │                │                  │                 │               │
    ├────────────────►│ WM_HOTKEY      │                  │                 │               │
    │                 ├─activated────► │ pos = cursor     │                 │               │
    │                 │                ├─grab()─────────► │ waiting for release │           │
    │                 │                │                  │ buffer snapshot │               │
    │                 │                │                  │ SendInput Ctrl+C│               │
    │                 │                │                  │ waiting for seq number │       │
    │                 │                │                  │ read, restore   │               │
    │                 │                │◄──cb(text)───────┤                 │               │
    │                 │                │ text empty? → showNoSelection(pos) ─────────────────► │
    │                 │                ├─showLoading(pos)────────────────────────────────────►│
    │                 │                ├─translate(req, Popup)───────────► │ cancel old     │
    │                 │                │                  │                 │ cache? → immediately │
    │                 │                │                  │                 │ service → HTTP │
    │                 │                │◄────────────── reply.finished ────┤                 │
    │                 │                ├─showResult / showError────────────────────────────► │
```

### 5.3. Ctrl+W — translation in the main window

Differs from Ctrl+Q:

- `AppController` first runs `SelectionGrabber::grab()` **without showing the window**: if shown
  and activated earlier, Ctrl+C would go to its own window instead of the program with the selection.
- After the callback, the window is shown and activated, the source text is inserted into the left field,
  the translation — into the right one. Translation starts immediately without waiting for the window to be shown:
  the window appears even before the service response.
- If there is no selection, the window simply opens with focus in the input field — this is a convenient way
  to "call the translator and type text".
- A repeated Ctrl+W during capture is ignored so as not to steal focus from the
  source program.

### 5.4. Ctrl+Shift+Q — translation with replacement of the selection

```
User           HotkeyMgr   AppController   SelectionGrabber   TranslationMgr   Windows
  │Ctrl+Shift+Q │              │                 │                │              │
  ├────────────►│ WM_HOTKEY    │                 │                │              │
  │             ├─activated──► │ target = foregroundWindow()      │              │
  │             │              ├─grab()──────────►│ buffer snapshot, Ctrl+C,     │
  │             │              │◄──text───────────┤ buffer restoration           │
  │             │              ├─translate(req, InPlace)──────────►│              │
  │             │              │◄──────────── finished ───────────┤              │
  │             │              ├─target still active? ────────────────────────────►│
  │             │              │   yes: buffer := translation, SendInput Ctrl+V,   │
  │             │              │       restore buffer after 500 ms                 │
  │             │              │   no: translation to clipboard + tray notification│
```

- The target window (`GetForegroundWindow`) is remembered **before** the capture. Our own window is not shown,
  so focus and selection remain in the input field; after the response, Ctrl+V replaces the selection.
- Translation goes through the same `TranslationManager` as Ctrl+Q, with the same smart language choice:
  with primary Russian and additional English, a Russian response is automatically translated into
  English. The `InPlace` channel does not cancel popup and main-window requests.
- If the user switched to another window while the request was in progress, the translation is **not inserted**:
  it is copied to the clipboard, and a notification is shown in the tray. This protects against insertion into
  a foreign field (including a password field).
- The clipboard is restored 500 ms after Ctrl+V. If `app/restoreClipboard` is disabled,
  the translation remains on the clipboard.
- No selection or a translation error → tray notification; the window does not appear.

### 5.5. Applying settings

```
SettingsDialog ──OK──► SettingsStore::save ──changed(old,new)──► AppController
                                                                    ├─ hotkeys changed → register/unregister only them
                                                                    ├─ autostart         → Autostart::sync
                                                                    ├─ languages/service → MainWindow/PopupWindow + TranslationManager
                                                                    ├─ maxLength         → TranslationManager + SelectionGrabber
                                                                    ├─ restoreClipboard  → SelectionGrabber
                                                                    ├─ network           → NetworkManager.applyProxy / setTimeoutMs
                                                                    ├─ popup width/focus → PopupWindow
                                                                    ├─ uiLanguage       → QTranslator + LanguageChange
                                                                    └─ service/keys     → service
```

While focus is in any `QKeySequenceEdit`, `SettingsDialog` emits
`hotkeyEditingChanged(true)`, and `AppController` suspends global hotkeys
(`HotkeyManager::setSuspended`). After focus leaves or the dialog closes, registration
is restored.

Changing the service in the tray or the main window calls `TranslationManager::setActiveService()`.
`activeServiceChanged` → `AppController::updateLanguageConfiguration()`: the language lists of both windows
are updated, an unsupported language is replaced via `Languages::closest`; the choice is saved to
`translation/service` via `SettingsStore::save()`.

### 5.6. Shutdown

"Quit" in the tray → `AppController::prepareForQuit()`: a shutdown flag (so that `closeEvent` does not hide
the window and `changed` is not applied again), saving the main window geometry to `SettingsStore`,
`HotkeyManager::unregisterAll()`, cancelling active requests, hiding the popup → `QApplication::quit()`.
A repeated call (`aboutToQuit`, session end) does nothing.

---

## 6. Error handling

| Situation | Where it is handled | What the user sees |
|---|---|---|
| Hotkey is taken | `AppController` during registration | Tray notification, warning in settings |
| No selection | `AppController` | "Nothing is selected" at the cursor / empty main window |
| No network, proxy error, TLS | service → `Network` | Error text and "Retry" in the translation window |
| Timeout | `Timeout` | "The service did not respond in N s" |
| Empty text | `TranslationManager` before the request → `EmptyText` | "Nothing to translate" |
| Invalid key | `Auth` | "Invalid DeepL API key" + "Open settings" link |
| Quota / frequent requests | `QuotaExceeded`, `RateLimited` | Explanation and advice to switch service |
| Free web service disabled | `TranslationManager` before the request → `ServiceUnavailable` | "The translation service is unavailable (Enable free web services in Settings → Services.)" |
| Free web endpoint blocked | service → `ServiceUnavailable` | Same title, details in the log; no retry storm |
| Unexpected response format | parser → `BadResponse` | "The translation service returned an unexpected response", details in the log |
| Service not configured | `TranslationManager` before the request | "DeepL requires an API key" + "Open settings" |

C++ exceptions are not used for flow control: errors are passed in `TranslationResult`.

---

## 7. Logging, security, privacy

- `QLoggingCategory` categories: `qtranslate.app`, `.hotkey`, `.grab`, `.net`, `.translation`
  (`TranslationManager`), `.service.google`, `.service.deepl`, `.settings`, `.autostart`, `.secret`.
  Only warnings are output by default; detailed logging is enabled by the
  `QT_LOGGING_RULES` environment variable or the `--verbose` flag (writing to
  `%LOCALAPPDATA%\qtranslate\qtranslate.log` on Windows or
  `$XDG_STATE_HOME/qtranslate/qtranslate.log` on Linux, rotation at 5 MB to `.1`). The message
  handler writes the time, level, category, and text; the directory is created at startup.
- **Texts being translated, keys, and passwords are never written to the log** — only lengths, response codes, time.
- All requests use HTTPS only; TLS errors are not ignored.
- Secrets in the INI use DPAPI on Windows and libsecret or a `0600` fallback on Linux (§4.11).
- In the help and when a service is selected for the first time, state that the text is sent to a third-party service
  (Google/DeepL).

---

## 8. Testing

Unit tests (Qt Test; logic comes from `qtranslate_core`, platform modules
are linked into separate test targets):

| Test | What it checks |
|---|---|
| `tst_appinfo` | Stable application name, semantic version, and user-agent string |
| `tst_googleparser` | Parsing of gtx and Cloud API responses by fixtures: multiple sentences, language detection, empty/broken response, Cloud error codes with a body |
| `tst_googletranslateservice` | The service against a local HTTP server: free/cloud modes, body/key, splitting long text and joining, HTTP errors, connection refusal, timeout, cancellation |
| `tst_deeplparser` | Parsing `translations[]`, per-part parsing, `/v2/usage`, error codes → `TranslationError` |
| `tst_deeplservice` | DeepL against a local HTTP server: Authorization, `text` array, English variants, Developer/Growth hosts, errors, timeout, key check |
| `tst_deeplwebparser` | Parsing the DeepL web endpoint response, error codes `403/429/413/5xx`, rate limit detection in the body |
| `tst_deeplwebservice` | DeepL free web against a local HTTP server: `Authorization: None`, language codes, 5000 characters, retry on `429`, serialization and a 2 s pause, errors, timeout |
| `tst_queuedwebservice` | The shared per-host queue on a fake service: one reply at a time, the rate pause, aborting a queued reply, deleting the active reply, `noteRequestSent()` restarting the pause, a multi-chunk reply holding its slot |
| `tst_openaichat` | Chat Completions request serialization (model, messages, temperature, `max_tokens`, stream), response parsing, error and finish-reason mapping, model list, endpoint building |
| `tst_openaicompatibleservice` | The OpenAI-compatible service against a local HTTP server: configuration errors, chat request, source language, plain-text and fenced-JSON responses, whitespace, HTTP errors, timeout, cancellation, OpenRouter key rules, model loading and edited base URL |
| `tst_languages` | Code mapping in both directions, `EN-US/EN-GB` variants, `PT-BR`, `deepl-web`, `closest` when the service changes, unknown codes |
| `tst_networkmanager` | Proxy types, HTTP proxy with authentication and `CONNECT`, SOCKS5 with login/password, `testProxy`, clear errors and no password in the text |
| `tst_popupplacement` | Positioning of the Ctrl+Q window: reflection at edges, pressing against the screen, a second monitor with negative coordinates |
| `tst_popupwindow` | Popup states (offscreen): request, result, copying, error/"Retry", language change, pinning, automatic hint closing |
| `tst_textchunker` | Splitting by paragraphs/sentences, words longer than the limit, surrogate pairs are not split |
| `tst_hotkeysequence` | `QKeySequence` ↔ `(MOD, VK)`, unsupported keys |
| `tst_hotkeymanager` | `RegisterHotKey`: shortcut conflicts, `setSuspended`, delayed registration (Windows; built together with `platform/hotkeymanager_win.cpp`) |
| `tst_hotkeymanager_x11` | X11 `XGrabKey`: registration, Lock/NumLock variants, auto-repeat, suspend/resume, Wayland refusal (16 cases) |
| `tst_hotkeymanager_wayland` | `GlobalShortcuts` portal against a mock DBus: session, batched bind, asynchronous failures, `Activated`/`Deactivated`, suspend, close (20 cases) |
| `tst_hotkey_selection_x11` | Real X11 manager + grabber + focused editor: release-order cases and the >300 ms hold (7 cases) |
| `tst_selectiongrabber_linux` | X11 selection capture, clipboard restore, Wayland clipboard read, timeouts (12 cases) |
| `tst_windowactivation_linux` | `_NET_ACTIVE_WINDOW`/`XGetInputFocus`, `_NET_WM_STATE_FULLSCREEN`, `_NET_WM_PID`, XTest paste, Wayland 0/false (13 cases) |
| `tst_cli` | CLI parser, mutual exclusion, two-process forwarding, legacy wire (20 cases, Linux) |
| `tst_settings` | Default values, writing/reading, version migration, portable path, DPAPI-compatible secret encryption |
| `tst_settingsdialog` | Filling tabs (including keys/mode/variant), returning the changed copy, checking shortcuts and languages, suspending global hotkeys, key check request (offscreen) |
| `tst_secretprotector` | Windows: DPAPI round-trip, empty and corrupted data. Linux: libsecret token round-trip, `0600` fallback, capability probe, migration (7+1 in trixie, 8/8 on Astra) |
| `tst_autostart` | Windows: `Run` command format. Linux: desktop entry quoting, `--minimized`, repair, removal (6 cases) |
| `tst_translationmanager` | Cancelling requests in a channel, cache, retry to the additional language — on a fake service |
| `tst_uilanguages` | Supported UI languages, normalization of `system`/regional tags, `.qm` resources |
| `tst_translations` | Completeness of all `.ts` files, preservation of `%1`/`%2`, emoji and edge whitespace |

Services are tested by substituting the network: a fake `TranslationService` for the manager and a
local HTTP server (`QTcpServer`) with predefined responses to check error codes,
timeouts, and proxies.

Windows manual checklist before release: selection capture in Notepad, a browser, Word, VS Code,
Telegram; a window in an administrator process (expected not to work); the clipboard after
translation matches the source; Ctrl+Q at screen edges and on two monitors with different scaling;
Ctrl+Shift+Q in an input field (selection replacement, clipboard restoration, protection when the
window changes); operation through HTTP and SOCKS5 proxies with authentication; autostart after a
reboot.

Linux manual checklist before release: on a real X11 session, Ctrl+Q/Ctrl+Shift+Q/Ctrl+W, the tray
menu, settings persistence, autostart create/remove, the three CLI commands with and without a
running instance, single-instance forwarding, and clipboard restoration; under a headless Weston,
the missing-portal warning, no X11 connection and no synthetic input; on a portal-capable desktop,
the `GlobalShortcuts` happy path (otherwise not performed). The Astra `.deb` install/remove and the
trusted-client step are checked on Astra; the trixie artifacts are checked on trixie and their
failure on Astra is recorded, never claimed as working.

### 8.1. Final verification results (old plan stage 8)

Checked manually on Release (Windows 11, one 3840×2160 monitor, 175% scaling):

- the UI language switches English ↔ Русский in settings without restart: the main window,
  the tray and its menu update immediately, the choice is saved in the INI;
- smart language on real Google: Russian text with the target "Russian" is automatically translated into
  English (two requests `ru → ru` and `ru → en` in the log), status "Detected language: Russian";
- cache: re-translating the same text takes ~3 ms, `Cache hit` in the log, no new network requests;
  errors are not cached (three 429 responses in a row did not create entries);
- DPI: `GetProcessDpiAwareness` = 2 (per-monitor), the window context is compared with
  `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`; the exe icon and the window `WM_GETICON` match
  `resources/icons/app.ico`;
- system mode and manual HTTP proxy (real proxy `127.0.0.1:2080`): translation works;
- Ctrl+Q in Notepad and Edge: the window shows the translation, the clipboard is restored;
  Ctrl+W moves the source and translation to the main window, the clipboard is also restored;
- Ctrl+Shift+Q in the Edge textarea: the selected Russian text was replaced with the English translation, the clipboard
  was restored; when switching to another window during the request, insertion did not happen,
  the translation remained on the clipboard and a notification was shown;
- Ctrl+Q at the bottom-right edge of the screen remains within the work area.

Environment limitations: Word and VS Code are not installed; Telegram is installed and authorized, but selection
in private chats was not tested; there is no second monitor with different scaling; a clean Windows without Qt is
unavailable (starting `build/dist/bin/qtranslate.exe` without Qt in `PATH` was tested); a real reboot
for autostart and a proxy with authentication were not tested; see §9.2.

### 8.2. CI and release verification 0.9.1

v0.9.0 — the first public release candidate; v0.9.1 — a patch with installer checkboxes and
updated documentation.

Local verification (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug and Release: build without `/W4` warnings, **17/17 tests**;
- `package.ps1` built `qtranslate-0.9.1-win64.zip` (22.4 MB),
  `qtranslate-0.9.1-win64-setup.exe` (16.6 MB) and `SHA256SUMS.txt`; both checksums
  matched the files;
- installer: silent installation into a separate directory, exe `FileVersion` = 0.9.1 (`qtranslate
  --version` → `qtranslate 0.9.1`), a Start menu shortcut was created; starting `bin\qtranslate.exe`
  without Qt in `PATH` succeeded, `Qt6Core.dll` and `vcruntime140.dll` loaded from the installation directory;
- `AppMutex`: with qtranslate running, the uninstaller exited with code 1 and did not delete files;
- installer tasks: by default the Start menu and autostart are enabled, the desktop is disabled;
  installation with `/TASKS=desktopicon` disables the menu and autostart; upgrading with
  `autostart=false` does not re-enable it; on uninstall the `Run` entry is removed;
- uninstall: exe and shortcut removed, the installation directory cleaned, `%APPDATA%\qtranslate\qtranslate.ini`
  preserved;
- portable zip: unpacked into a separate directory, `bin\qtranslate.exe` started without Qt in
  `PATH`, Qt and CRT loaded from the archive.

CI artifacts (Qt 6.8.3 LTS, `windows-latest`) were downloaded and checked separately: zip 22.5 MB,
installer 16.4 MB; installation, starting without Qt in `PATH`, uninstall, and checksums — all in order.
CI: the workflow `.github/workflows/build.yml` builds and tests Release on `windows-latest`,
then repeats packaging; on the `v0.9.1` tag it publishes the release.

- workflow: https://github.com/youkaicat/QTranslate/actions/workflows/build.yml
- successful tag run: https://github.com/youkaicat/QTranslate/actions/runs/41908976474
- release: https://github.com/youkaicat/QTranslate/releases/tag/v0.9.1

Files downloaded from the release matched `SHA256SUMS.txt`.

### 8.3. CI and release verification 0.10.0

Local verification (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug and Release: build without `/W4` warnings, **21/21 tests**;
- `package.ps1` built `qtranslate-0.10.0-win64-setup.exe` (11.51 MB),
  `qtranslate-0.10.0-win64.zip` (14.58 MB) and `SHA256SUMS.txt`; checksums verified;
- `build/dist` — 35.0 MB; the ZIP has 41 entries, 13 Qt translations, `opengl32sw.dll` is absent;
- installer: silent installation into a temporary directory, `FileVersion`/`ProductVersion` = 0.10.0,
  starting `bin\qtranslate.exe` without Qt in `PATH`, uninstall; `%APPDATA%\qtranslate\qtranslate.ini`
  and `HKCU\...\Run` restored after the check;
- live smoke test of DeepL free web through the built service: `Hello, world` → `Hallo, Welt`,
  `detected_source_language=en`;
- CI (Qt 6.8.3 LTS): setup 10.63 MB, ZIP 13.12 MB; hashes matched `SHA256SUMS.txt`;
  the CI installer was checked by silent installation, launch, and uninstall;
- CI tag run: https://github.com/youkaicat/QTranslate/actions/runs/41930568365
- release: https://github.com/youkaicat/QTranslate/releases/tag/v0.10.0
- old tags `v0.9.0`/`v0.9.1` are not changed.

### 8.4. CI and release verification 0.11.0

Local verification (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug and Release: build without `/W4` warnings, **23/23 tests**;
- `package.ps1` built `qtranslate-0.11.0-win64-setup.exe` (12 097 066 bytes),
  `qtranslate-0.11.0-win64.zip` (15 333 372 bytes) and `SHA256SUMS.txt`;
- `build/dist` — 36 845 376 bytes; the ZIP has 41 entries, 13 Qt translations;
- installer: silent installation into a temporary directory, `ProductName=QTranslate`,
  `FileVersion`/`ProductVersion` = 0.11.0, starting without Qt in `PATH`, uninstall; the Run entry
  of autostart is created and removed, `%APPDATA%\qtranslate\qtranslate.ini` and `HKCU\...\Run`
  restored after the check;
- live smoke test of OpenRouter through SOCKS5 `127.0.0.1:2080`: `Hello, world` → `Привет, мир`,
  `openrouter/free`, HTTP 200;
- the OpenAI-compatible provider works with a configurable base URL, key, and model; DeepL free web
  is merged into the `free-web` mode of a single DeepL service;
- CI (Qt 6.8.3 LTS): setup 11 173 822 bytes (10.66 MB), ZIP 13 795 207 bytes (13.16 MB);
  hashes matched `SHA256SUMS.txt`; the CI installer was checked by silent installation, launch without Qt in
  `PATH`, and uninstall; `ProductName=QTranslate`, `ProductVersion=0.11.0`;
- CI tag run: https://github.com/youkaicat/QTranslate/actions/runs/41943741295
- CI `main` run: https://github.com/youkaicat/QTranslate/actions/runs/41943729918
- release: https://github.com/youkaicat/QTranslate/releases/tag/v0.11.0

---

## 9. Accepted decisions and development

### 9.1. Accepted decisions

| Question | Decision |
|---|---|
| Ctrl+Q / Ctrl+W are captured from all programs | Keep as default, without warnings; they can be changed in settings (§4.3) |
| Ctrl+Alt on a non-English layout | Qt may return `GroupSwitchModifier` (AltGr); normalize it to `MOD_CONTROL \| MOD_ALT`, otherwise the combination would be rejected incorrectly (§4.3) |
| HTTPS proxy | HTTP proxy with a `CONNECT` tunnel; TLS to the proxy is not supported (§4.10) |
| Windows of processes launched as administrator | Selection capture does not work in them — this is a known limitation, we do not work around it (§4.4) |
| Pinning 📌 in the popup window | Active until the window is closed; a new Ctrl+Q starts in an unpinned state (§4.13) |
| HTTP/2 and free Google | Force HTTP/1.1: gtx consistently returned `429` for Qt HTTP/2 requests, works over HTTP/1.1 (§4.10) |
| Translation with replacement of the selection | A separate Ctrl+Shift+Q action; does not change Ctrl+Q. Insert only if the original window is still active; otherwise the translation is copied to the clipboard and a notification is shown (§5.4) |
| Installer | Inno Setup 6.3+/7, **per-user** installation without UAC to `%LOCALAPPDATA%\Programs\qtranslate`; Start menu (on), desktop (off), and autostart (on) tasks; autostart is synchronized with the application setting; settings are preserved on uninstall (§3.4) |
| Portable build | Remains the second release artifact: a ZIP with `bin/`, `plugins/`, `translations/`; needed by users without installation rights (§3.4) |
| CI and releases | GitHub Actions with a Windows job on `windows-latest` and a Linux job in `debian:trixie`; push/PR build and test, the `v*` tag publishes the installer, ZIP, `SHA256SUMS.txt`, the Linux tar.gz/`.deb`/AppImage, and `SHA256SUMS-linux.txt`; the tag version is checked against CMake in the Windows job (§3.4) |
| Checksums | `SHA256SUMS.txt` remains a separate release asset but is not listed in README/Downloads; if needed, the user can find the file |
| Linux | Linux x86_64 is supported from 0.12.0: X11 is a full target, Wayland is a portal/CLI fallback with explicit limitations; trixie artifacts are CI-built and Astra needs a native build; macOS is not planned (§4.14) |
| Win+V history entry during capture | The intermediate Ctrl+C may leave an entry; exclusion formats protect only clipboard restoration. A full workaround is a UI Automation strategy (§4.4, §9.2), accepted as future development |

### 9.2. Limitations and possible development

Limitations of the environment in which the final verification was performed (not application defects):

- Word and VS Code are not installed — selection capture in them was not tested; Telegram is authorized, but
  private chats were not opened.
- There is no second monitor with different scaling; one 3840×2160 monitor at 175% was tested.
- A clean Windows without Qt is unavailable; starting the self-contained `build/dist` without Qt in `PATH` was tested.
- A real Windows reboot for autostart and an external proxy with authentication were not tested
  (local HTTP/SOCKS5 proxies with authentication are covered by `tst_networkmanager`).
- Capture in windows of administrator processes and the Win+V entry are known limitations (§9.1).
- The installer is not code-signed: Windows SmartScreen may show a warning. There is no certificate
  in the project; options and requirements are described in [`code-signing.md`](code-signing.md).
  Adding a signature does not change the artifact format.
- Linux is implemented on `main` for 0.12.0 (29 tests, X11 full, Wayland limited); the 0.11.x
  releases remain Windows-only, so the Linux packages appear only with 0.12.0. The trixie artifacts
  do not run on Astra 1.8.7, which needs the separate native build (§4.14). macOS is not planned.

Possible development:

1. **Capture via UI Automation** without using the clipboard (§4.4): the "UIA →
   clipboard" chain, TextPattern/GetSelection, a fallback for programs without UIA.
2. **Dictionary entries** — a separate service/panel, not part of translation: LLM via a JSON schema,
   Google gtx `dt=bd` for short texts, a local CSV glossary, and Wiktionary. Show
   parts of speech, meanings, examples, synonyms, and transcription for individual words and short phrases.
   Design — [`dictionary.md`](dictionary.md); the Google format is undocumented.
3. Additional official services (Microsoft Translator, Amazon Translate, Yandex,
   PROMT, LibreTranslate) — a new `TranslationService` class.
4. Text-to-speech (`QTextToSpeech`), translation history.
5. **Linux** — implemented for 0.12.0: X11 `platform/*`, Wayland via the `GlobalShortcuts` portal
   and a CLI fallback, `.deb`/AppImage/Astra packages (§4.14); remaining work is the real
   portal-capable Wayland check and a broader desktop matrix.
6. **Localization into major world languages** — old plan stage 12 is done: Spanish, German, French,
   Portuguese (Brazil), Italian, Polish, Ukrainian, Turkish, Chinese
   (Simplified), Japanese, Korean, Arabic, Hindi; RTL and translation checks in CI (§4.13).
   Translations require selective native-speaker review before a public release.
7. **Code signing** of the installer ([`code-signing.md`](code-signing.md)) and auto-update over
   the installed version.
8. **Free web endpoints** — DeepL web is already implemented as the `free-web` mode of a single
   "DeepL" service without a key (§4.7.1); Yandex web, Bing web, Reverso, Mozhi, MyMemory remain
   experimental. Rate limits and an unofficial-status warning apply to all.
   Details and priorities — [`free-endpoints.md`](free-endpoints.md).
9. **Local offline translation** — Argos models + CTranslate2 without Python: model download by
   button, pivot through English, fallback to the cloud. Details and PoC — [`local-translation.md`](local-translation.md).
10. **Spelling, grammar, and punctuation checking** — a separate tab, LanguageTool/Hunspell/
    Yandex Speller, replacing text with the corrected one. Details — [`proofreading.md`](proofreading.md), §4.15.
11. **LLM actions** — translation already works via an OpenAI-compatible provider (OpenRouter by
    default, cloud or local); checking, summarization, rewriting, explanation, and pronunciation
    are planned. The dictionary is a separate `DictionaryService` ([`dictionary.md`](dictionary.md)).
    Details — [`llm-actions.md`](llm-actions.md), §4.16.

---

## 10. Implementation order

The step-by-step plan by stages is in the active roadmap [`roadmap/qtranslate/plan.md`](roadmap/qtranslate/plan.md); the detailed history of the old flat plan was moved to [`roadmap/qtranslate/legacy-plan.md`](roadmap/qtranslate/legacy-plan.md) in stage 1 of the migration.

---

## 11. Documentation and roadmap

### 11.1. Context

Before the migration, the project documentation was Russian and authoritative: `doc/architecture.md`, `doc/plan.md`, `doc/handoff.md`, and the design notes in `doc/`. `README.md` is English, `README.ru.md` is Russian, and the release notes in `doc/releases/` are already English. The flat plan, archived as `doc/roadmap/qtranslate/legacy-plan.md`, contains the detailed history of stages 1-12, the partial work on stages 13 and 16, and the remaining stages 11 and 13-17. This is a personal project with no Jira issue numbers; commit messages follow the repository's existing style without a Jira key.

The lead agent works with a tree roadmap rather than a flat plan. The existing documents did not provide that structure, so the project needed a one-time documentation migration and an English baseline, which the migration in stage 1 provided.

### 11.2. Decision

Adopt a tree roadmap under `doc/roadmap/`, keep one active map for the project, and move the active documentation to English. Preserve the old flat plan as a Russian historical archive next to the new map. Do not change product code during the migration, with one approved exception: the stale `doc/plan.md` reference in the comment at `src/platform/selectiongrabber_win.cpp:248` is repointed to the archive, and nothing else in the code changes.

The active map is `doc/roadmap/qtranslate/plan.md`. Stage 1 archived the old plan as `doc/roadmap/qtranslate/legacy-plan.md`. The map list is `doc/roadmap/README.md`. The migration is the first stage of the new map; the remaining product work follows the old plan order: Linux, free web endpoints, local offline translation, proofreading, LLM actions, and the dictionary.

### 11.3. Repository layout

```
doc/
  architecture.md
  handoff.md
  code-signing.md
  dictionary.md
  free-endpoints.md
  llm-actions.md
  local-translation.md
  proofreading.md
  releases/
    ...
  roadmap/
    README.md
    qtranslate/
      plan.md
      legacy-plan.md
      journal.md
      01-documentation-migration/
        plan.md
        ...
      02-linux/
        plan.md
      ...
```

- `doc/roadmap/README.md` lists maps by slug, status, one-line description, and link.
- `doc/roadmap/qtranslate/plan.md` is the root node (`kind: root`) and the active roadmap.
- `doc/roadmap/qtranslate/legacy-plan.md` is the Russian original of the old flat plan. It is a file next to the root plan, not a roadmap node. It is kept for history and rationale.
- `doc/roadmap/qtranslate/journal.md` is created and maintained by the lead. It records "Where we left off" and decisions. It is not a roadmap node.
- Stage directories (`NN-slug/plan.md`, `kind: stage`) and task directories (`NN-slug/plan.md`, `kind: task`) are created by the lead and its planner. The architect does not create them.
- A roadmap node is a directory containing `plan.md`. Other files and directories in the map are not nodes.

### 11.4. Language policy

- Active project documentation is English: `doc/architecture.md`, `doc/handoff.md`, the design notes in `doc/`, `i18n/README.md`, `AGENTS.md`, and the roadmap.
- `README.md` stays English and `README.ru.md` stays Russian as the user-facing localisation. Keep them in sync.
- Code comments are not translated in this migration. Translating them is a separate task and would create unrelated code churn.
- The archived `legacy-plan.md` stays Russian. It is not active documentation and is clearly marked as historical.
- Release notes in `doc/releases/` are already English; keep them English.
- English text may quote a Russian UI string where the distinction matters. Prefer the English source string; keep the quoted Russian only when the Russian UI label itself is the subject.
- The roadmap root uses `lang: en`. Headings and field names in the map are English.

### 11.5. Roadmap format

The lead's tree rules define the generic workflow. The project records the map location, language, and stage order in this document and in `AGENTS.md`.

Root node:

```yaml
---
kind: root
status: todo
lang: en
source: doc/architecture.md
---
```

The root body contains the title, goal, `## Done when`, a short `## History`, and `## Stages`. The history summarises completed work and links to the detailed Russian archive. The stages list the remaining work in order. Stage directories are created when the lead starts each stage.

Stage node:

```yaml
---
kind: stage
status: todo
---
```

The stage body contains the title, goal, what to do and what is touched, `## Done when`, depends on, and risks plus what is needed from the user. The lead's planner splits the stage into tasks when the stage starts.

Task node:

```yaml
---
kind: task
status: todo
detailed: true
---
```

The task body contains `## Task`, `## Steps`, `## Done when`, and, after completion, `## Outcome`. The coordinator fills the outcome after the commit.

Statuses are `todo`, `in-progress`, `done`, `blocked`, and `dropped`. A blocked or dropped node states the reason in its body. `jira` is absent because this project has no Jira key. The lead asks the user about the branch and the commit/push policy when the roadmap starts; the project rules in `AGENTS.md` govern pushing.

### 11.6. Migration

The migration is stage 1 of the roadmap. The lead performs these steps:

1. Create the stage directory and tasks; the roadmap README and root plan already exist.
2. Move `doc/plan.md` to `doc/roadmap/qtranslate/legacy-plan.md`. Add an English header that marks the file as a historical Russian archive and points to the active `plan.md`. Update its relative links to `../../...` so they still resolve.
3. Translate `doc/architecture.md` sections 1-10 to English, keep section 11, and translate the title and headings. Section 10 already points to the new roadmap.
4. Translate `doc/handoff.md` to English. Update the release process to reference the roadmap and the legacy plan instead of the old flat plan. Record the manual checks that were never completed as known gaps.
5. Translate the design notes: `doc/code-signing.md`, `doc/dictionary.md`, `doc/free-endpoints.md`, `doc/llm-actions.md`, `doc/local-translation.md`, `doc/proofreading.md`, and `i18n/README.md`.
6. Update design-note stage references to the new roadmap numbering. Keep the old plan stage numbers in parentheses where they help trace history.
7. Update `AGENTS.md`: English design docs, roadmap location, archive location, release process, and the absence of a Jira key.
8. Search for `doc/plan.md` references. Update active documents to the roadmap or the archive; keep historical references only in the archive and release history.
9. Update `README.md` or `README.ru.md` only if a link actually changed. Keep the READMEs in sync.
10. Verify links, English prose, roadmap frontmatter, and that no code changed except the one approved
    comment line in `src/platform/selectiongrabber_win.cpp`.

### 11.7. References and compatibility

- The old plan's links to `architecture.md`, `free-endpoints.md`, `local-translation.md`, `proofreading.md`, `llm-actions.md`, and `dictionary.md` become `../../...` after the move. The archive header explains the change.
- Design notes that cite "stage 13", "stage 14", and so on now cite the new roadmap stage and keep the old number in parentheses. The old plan remains the detailed history.
- Release notes are English and do not need translation. Update them only if they link to the old plan.
- Code comments and source files are not touched, except the one approved comment line in
  `src/platform/selectiongrabber_win.cpp` that now points to the archive. Any other documentation
  change that would require a code comment change is deferred to a separate task.
- The migration is documentation-only except the one approved comment line in `src/platform/selectiongrabber_win.cpp`, which needs no build; any other code or test change requires the relevant Debug and Release checks and all 24 tests.

### 11.8. Verification

- Run the lead's tree overview command and confirm the root plan has valid `kind`, `status`, and `lang`.
- Check every relative Markdown link in `doc/` resolves to an existing file.
- Search for Cyrillic in active documentation, excluding `README.ru.md`, `doc/roadmap/qtranslate/legacy-plan.md`, and quoted Russian UI strings. Review headings and paragraphs manually; a raw grep can produce false positives for quoted labels.
- Search for `doc/plan.md` and `plan.md` references. Active docs should point to the roadmap; only the archive and historical notes may point to the old plan.
- Confirm `git diff --name-only` contains only documentation, `AGENTS.md`, and the approved comment
  line in `src/platform/selectiongrabber_win.cpp`. No other source, test, build, or packaging file
  changes.
- Ask the user to spot-check `doc/architecture.md` and `doc/handoff.md`, the two documents most likely to affect future work.
- Do not push without an explicit user request. Follow the commit rules in `AGENTS.md`.

### 11.9. Accepted decisions

| Question | Decision | Reason | Rejected alternative |
|---|---|---|---|
| Documentation language | English for active docs | User request; consistent technical docs; the lead works in English | Keep Russian; translate only the architecture; translate everything including `README.ru.md` |
| `README.ru.md` | Keep Russian | It is the user-facing Russian localisation and must stay in sync with `README.md` | Translate it; remove it |
| Code comments | One approved comment line; everything else unchanged | Separate task; avoids unrelated code churn; the stale path would otherwise point to a deleted file | Translate now |
| Old flat plan | Archive in Russian as `doc/roadmap/qtranslate/legacy-plan.md` | Preserves details and original wording; user choice | Delete it; translate it fully; keep it as the active plan |
| Roadmap location | `doc/roadmap/qtranslate/plan.md` | Project docs live in `doc/`; one map for the project | `docs/`; a separate map for the migration |
| Stage order | Migration first, then old order: Linux, web endpoints, local offline, proofreading, LLM actions, dictionary | User choice; preserves the old plan order | Linux last; migration-only map; features before migration |
| Completed stages | Brief English history in the root; details in the Russian archive | Avoids duplicating 1,488 lines; keeps history | Full translation; no history |
| Legacy manual checks | Record in `doc/handoff.md` as known gaps | Releases already shipped; they do not block new work | New acceptance stage; ignore them |
| Jira key | None | Personal project; user confirmed | Invent a key; ask the user again |
| Commit and push | Follow `AGENTS.md`; no push without explicit request | Project rules; GitHub Actions load | Push every commit; push automatically |
| Verification | Link check, English review, frontmatter check, no code changes except the one approved comment line | Documentation-only migration, except the one approved comment line | Full build and test; not needed for docs |

### 11.10. Risks and mitigations

- **Translation inaccuracies.** Technical English may change a nuance. Keep the Russian archive as the original wording and ask the user to spot-check the architecture and handoff.
- **Broken links after the move.** Update every relative link in the archive and active docs, then run a link check.
- **Mixed language during migration.** The architecture document was mixed while the migration ran; keeping the migration in one stage and prioritising the architecture and handoff limited that period.
- **Stale `AGENTS.md`.** It is loaded in every session. Update it in stage 1 and search for old paths.
- **Archive mistaken for the active plan.** Add a clear English header and list the archive in the map README and `AGENTS.md`.
- **Translation volume.** The active docs total roughly 4,000 lines. Split the work into tasks; start with the architecture and handoff, then the design notes, then `i18n/README.md`.
- **Linux stage may block.** The old order puts Linux first after migration. It needs a Linux machine and CI. If it is blocked, the lead asks the user whether to start independent stages earlier.
- **Release process references.** `doc/handoff.md` and `doc/code-signing.md` mention the old plan. Update them during migration.
- **Quoted Russian UI strings.** English docs may need to quote a Russian label. Allow quoted strings; do not treat them as unfinished translation.
- **Machine-only translation.** It can be inaccurate. Use human-reviewed technical English.
- **Git history and releases.** Do not push without an explicit request. Do not create a release during the migration.

### 11.11. Open items

- **Branch.** The user decided to continue on `main`; no separate branch was created.
- **Commit and push policy.** The lead asks and follows `AGENTS.md`. No push without an explicit request.
- **Archive name and location.** The archive is `doc/roadmap/qtranslate/legacy-plan.md`; stage 1 moved the file there.
- **Translation glossary.** Use technical English and preserve identifiers, commands, paths, and product names. The user can spot-check and request terminology changes.
- **No Jira key.** Confirmed by the user. The lead must not ask for a Jira number.
- **Stage order.** Migration first, then the old order. Confirmed by the user. If Linux is blocked, the lead may ask to start independent stages earlier.

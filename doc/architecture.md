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

- C++17, Qt 6.5+ (Windows: Qt 6.12, MSVC 2022 x64; Linux: GCC/Clang — roadmap stage 2 (old stage 11)), CMake ≥ 3.19.
- Qt modules: `Core`, `Gui`, `Widgets`, `Network`. Tests additionally use `Test`.
- Platforms: **Windows 10/11 x64 — current**; **Linux x86_64 — planned** (roadmap stage 2 (old stage 11));
  macOS is not planned. Platform code is isolated (see §3.3 and §4.14) so the port does not touch the core.
- TLS: on Windows the stock Qt 6 Schannel backend (`qschannelbackend`), OpenSSL is not needed; on Linux
  the Qt OpenSSL backend will be used.
- WinAPI (Windows only): `user32` (hotkeys, `SendInput`, clipboard), `crypt32` (DPAPI for secrets).

### 1.4. Platform matrix

| Component | Windows 10/11 x64 (0.11.0) | Linux x86_64 (planned, roadmap stage 2 (old stage 11)) |
|---|---|---|
| Build | MSVC 2022, Qt 6.12 | GCC/Clang, Qt 6.5+, CMake |
| UI | Qt Widgets, tray | Qt Widgets, StatusNotifierItem/AppIndicator |
| Hotkeys | `RegisterHotKey` | X11 `XGrabKey`; Wayland — the `GlobalShortcuts` portal or CLI + desktop shortcuts |
| Selection capture | `SendInput` Ctrl+C + clipboard | X11 XTest; Wayland — the user copies manually |
| Pasting the translation | `SendInput` Ctrl+V | X11 XTest; Wayland — not reliably available |
| Autostart | `Run` registry key | XDG autostart |
| Secrets | DPAPI | libsecret; a 0600 fallback file |
| Settings | `%APPDATA%\qtranslate` | `$XDG_CONFIG_HOME/qtranslate` |
| Packages | installer + portable zip | `tar.gz`, `.deb`, AppImage |

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
│   └── windows/
│       ├── qtranslate.iss      Inno Setup installer
│       └── package.ps1         dist + zip + setup + SHA256SUMS
├── doc/
│   ├── architecture.md
│   ├── handoff.md
│   ├── code-signing.md
│   ├── dictionary.md
│   ├── free-endpoints.md
│   ├── llm-actions.md
│   ├── local-translation.md
│   ├── proofreading.md
│   ├── releases/0.11.0.md      release notes
│   └── roadmap/
│       ├── README.md
│       └── qtranslate/
│           ├── plan.md
│           ├── legacy-plan.md
│           ├── journal.md
│           └── 01-documentation-migration/plan.md
├── resources/
│   ├── qtranslate.qrc          tray and window icons
│   ├── icons/                  app.ico, tray-*.png (light/dark theme)
│   └── qtranslate.rc           exe icon and version
├── i18n/
│   └── qtranslate_ru_RU.ts     UI translation
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── appcontroller.h/.cpp
│   │   ├── logging.h/.cpp            qtranslate.app logging category
│   │   └── singleinstance.h/.cpp
│   ├── core/                   ← static library qtranslate_core (no Widgets or WinAPI)
│   │   ├── appinfo.h/.cpp            application name and version
│   │   ├── translationtypes.h        TranslationRequest/Result/Error
│   │   ├── translationservice.h/.cpp service interface + TranslationReply
│   │   ├── translationmanager.h/.cpp service registry, cache, language logic, cancellation
│   │   ├── languages.h/.cpp          language table and code mapping
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
│   │   └── deeplparser.h/.cpp
│   ├── platform/
│   │   ├── hotkeymanager.h           platform-independent interface
│   │   ├── hotkeymanager_win.cpp
│   │   ├── hotkeymanager_stub.cpp    stub for non-Windows builds
│   │   ├── selectiongrabber.h
│   │   ├── selectiongrabber_win.cpp
│   │   ├── selectiongrabber_stub.cpp stub for non-Windows builds
│   │   ├── autostart.h
│   │   ├── autostart_win.cpp
│   │   ├── secretprotector.h
│   │   ├── secretprotector_win.cpp   DPAPI
│   │   ├── windowactivation.h        right to bring a window to the foreground
│   │   ├── windowactivation_win.cpp  AllowSetForegroundWindow
│   │   └── windowactivation_stub.cpp stub for non-Windows builds
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
    ├── tst_googleparser.cpp
    ├── tst_googletranslateservice.cpp  service against a local HTTP server
    ├── tst_deeplparser.cpp
    ├── tst_deeplservice.cpp
    ├── tst_languages.cpp
    ├── tst_networkmanager.cpp
    ├── tst_textchunker.cpp
    ├── tst_translationmanager.cpp
    ├── tst_hotkeysequence.cpp
    ├── tst_popupplacement.cpp
    ├── tst_popupwindow.cpp
    ├── tst_secretprotector.cpp
    ├── tst_settings.cpp
    ├── tst_settingsdialog.cpp
    └── tst_autostart.cpp
```

### 3.2. Mapping to the current `CMakeLists.txt`

The §3.1 structure is the target: directories and files appear and are added to targets as the stages
are implemented. Currently:

| Target | What is included now (0.11.0) |
|---|---|
| `qtranslate_core` | `ai/openaichat`; `core/`: `appinfo`, `hotkeysequence`, `languages`, `popupplacement`, `settings`, `textchunker`, `translationtypes`, `translationservice`, `translationmanager` (cache, smart language); `net/networkmanager`; `services/`: `googleparser`, `googletranslateservice`, `deeplparser`, `deeplservice`, `deeplwebparser`, `deeplwebservice`, `openaicompatibleservice` |
| `qtranslate` | `main.cpp`, `app/*` (including the file log), `ui/*` (`appicon`, `languagecombobox`, `mainwindow` with auto-translation, `popupwindow`, `settingsdialog`, `translationerrortext`, `trayicon`), `platform/*`, `resources/qtranslate.rc`, `resources/icons/app.ico`, `i18n/qtranslate_*.ts` |
| tests (`tests/CMakeLists.txt`) | `tst_appinfo`, `tst_autostart` (Windows), `tst_deeplparser`, `tst_deeplservice`, `tst_deeplwebparser`, `tst_deeplwebservice`, `tst_googleparser`, `tst_googletranslateservice`, `tst_hotkeymanager` (Windows), `tst_hotkeysequence`, `tst_languages`, `tst_networkmanager`, `tst_popupplacement`, `tst_popupwindow` (offscreen), `tst_secretprotector` (Windows), `tst_settings`, `tst_settingsdialog` (offscreen), `tst_textchunker`, `tst_translationmanager` (cache and smart retry), `tst_openaichat`, `tst_openaicompatibleservice`; each test is added by the `qtranslate_add_test(<name>)` function, links against `qtranslate_core`, and receives the `tests/fixtures` path in the `QTRANSLATE_FIXTURES_DIR` macro |

### 3.3. Build

- `qtranslate_core` — a `STATIC` library from `core/`, `net/`, `services/`; depends on `Qt::Core`,
  `Qt::Gui` (because of `QKeySequence` in `hotkeysequence`) and `Qt::Network`, but not on `Qt::Widgets`
  and does not use WinAPI. Both the application and the tests link against it.
- `qtranslate` — the executable (`WIN32`): `app/`, `platform/`, `ui/`, resources; links against
  `qtranslate_core`, `Qt::Widgets`, `user32`, `crypt32`.
- Platform files are included under `if(WIN32)`; for other platforms each module has a stub so the
  targets build. Headers in `platform/` do not contain `<windows.h>` — WinAPI is visible only in
  `*_win.cpp`.
- UI translation: `qt_add_translations(qtranslate TS_FILES <all i18n/qtranslate_*.ts>)`, the `.qm`
  files are embedded in resources.
- `enable_testing()` + `add_subdirectory(tests)`, each test is a separate `qt_add_executable` and
  `add_test`.
- Deployment uses `qt_generate_deploy_app_script` (windeployqt). The distribution includes the
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

### 3.4. Packaging, CI and release

The project version is defined once — `project(qtranslate VERSION 0.11.0)`; `AppInfo::version()`, the
`User-Agent`, the exe version resource, and artifact names are derived from it. The release tag is
`v0.11.0`; CI compares it with `CMakeLists.txt` and fails on a mismatch.

**Portable archive.** `cmake --install <build> --prefix build/dist` creates a self-contained `bin/`,
`plugins/`, `translations/` directory with Qt, plugins, and the MSVC CRT. `package.ps1` checks the
required files and packs the contents of `dist` into `qtranslate-<version>-win64.zip`; the archive
contains the same `bin/`, `plugins/`, `translations/`, and is started via `bin\qtranslate.exe`.

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

**CI.** `.github/workflows/build.yml` on `windows-latest`:
1. `install-qt-action` installs Qt 6.8.3 LTS MSVC 2022 x64 (aqtinstall does not yet support the Qt 6.12
   repository layout; local development uses 6.12, the project requires 6.5+);
2. `ilammy/msvc-dev-cmd` sets up MSVC, CMake (Ninja) builds Release;
3. `ctest` runs all tests with `QT_QPA_PLATFORM=offscreen`;
4. Chocolatey installs Ninja and Inno Setup, `package.ps1` builds the artifacts;
5. `upload-artifact` stores `build/artifacts/*`.

The workflow runs on push to `main`, on pull requests, and manually. On a `v*` tag a separate job with
`contents: write` downloads the artifacts and creates/updates the GitHub Release, attaching the
installer, the zip, and `SHA256SUMS.txt`; the notes come from `doc/releases/<version>.md`. The checksum
file remains an asset but is not advertised in the README/Downloads. A repeated run overwrites the
release files (`gh release upload --clobber`).

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
- Requests to the host are serialized inside the service and go no more often than once every 2 s,
  including requests from the main window, the popup, and selection replacement. On `429` — a 3 s
  pause and one retry. `403` is treated as a block/limit (`RateLimited`), `413` → `TooLong`,
  `5xx` → `Network`.
- `isConfigured() == true`; no key or cookies are used. The text and response body are not written to
  the log.
- Tests — `tst_deeplwebparser`, `tst_deeplwebservice`, and the `free-web` mode in `tst_deeplservice`;
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

### 4.8. Языки — `languages`

Внутри программы используются собственные коды языков (`en`, `ru`, `de`, `pt-BR`, `zh-Hans`…).
Сервисы переводят их в свои коды:

| Внутренний | Google | DeepL source | DeepL target |
|---|---|---|---|
| `en` | `en` | `EN` | `EN-US` (настраивается: `EN-GB`) |
| `pt-BR` | `pt` | `PT` | `PT-BR` |
| `zh-Hans` | `zh-CN` | `ZH` | `ZH-HANS` |
| `ru` | `ru` | `RU` | `RU` |
| `he` | `iw` | `HE` | `HE` |
| `nb` | `no` | `NB` | `NB` |
| `fil` | `tl` | — | — |

Модуль хранит таблицу `{код, английское название, название на родном языке, коды сервисов}`
(около 110 языков Google, из них около 35 есть у DeepL) и предоставляет функции в пространстве имён
`Languages`:

- `toServiceCode(serviceId, code, direction)` — пустая строка, если сервис язык не поддерживает;
- `fromServiceCode(serviceId, serviceCode, direction)` — без учёта регистра; сначала код нужного
  направления, затем другого (DeepL `EN-US` как исходный, `PT` как целевой), затем синонимы
  (Google `he`/`jv`/`zh`, DeepL `EN-GB`); неизвестный код — пустая строка. Обратное
  преобразование нужно для `detectedSourceLang`;
- `supported(serviceId, direction)` — списки языков для сервисов и `LanguageComboBox`;
- `closest(serviceId, code, direction)` — ближайший поддерживаемый код: точный, затем язык той же
  основы (`pt-BR`/`pt-PT`, `zh-Hans`/`zh-Hant`), системный, `en`, первый доступный;
- `displayName(code)` — название для UI, помечено `QT_TRANSLATE_NOOP` (контекст `Languages`) под
  локализацию этапа 8; `englishName(code)` — стабильное английское имя для промптов LLM, не
  зависящее от языка интерфейса; `nativeName(code)` — самоназвание (подсказка в списке);
- `autoCode()` — `"auto"`; `forLocale(QLocale)` / `systemLanguage()` — язык системы с учётом
  письменности и региона (`zh-Hans`/`zh-Hant`, `pt-BR`/`pt-PT`, нюнорск → `nb`), иначе `en`.
- Для сервиса `llm` используются внутренние коды языка и весь каталог Google: LLM не требует
  отдельной таблицы кодов, а `englishName()` даёт модели однозначное название языка.

Списки языков у сервисов разные (у DeepL их меньше). `LanguageComboBox` показывает только
поддерживаемые выбранным сервисом; при смене сервиса `AppController` заменяет текущий язык через
`Languages::closest` (например, `pt` → `pt-BR`, `fil` → системный/`en`). «Auto-detect» остаётся
исходным языком всегда.

### 4.9. `TranslationManager`

Фасад над сервисами, с которым работают `AppController` и UI.

```cpp
class TranslationManager : public QObject {
    Q_OBJECT
public:
    enum class Channel { MainWindow, Popup, InPlace };

    void registerService(TranslationService *service);   // забирает владение; первый — активный
    QList<TranslationService *> services() const;
    TranslationService *service(const QString &id) const;
    TranslationService *activeService() const;
    void setActiveService(const QString &id);
    void setMaxLength(int length);                         // 20 000 по умолчанию; из настроек — этап 5

    // channel — «канал» запроса: Popup, MainWindow или InPlace.
    // Новый запрос в том же канале отменяет предыдущий незавершённый.
    TranslationReply *translate(const TranslationRequest &req, Channel channel);
    void cancel(Channel channel);
    void cancelAll();                                      // при завершении программы (§5.6)
signals:
    void activeServiceChanged(const QString &id);
};
```

Менеджер хранит на текущий ответ каждого канала только `QPointer`: ответом владеет вызывающий.
`translate()` сначала проверяет кэш, затем оборачивает ответ сервиса в `Reply` (кэширование и
повтор на дополнительный язык); интерфейс менеджера от этого не меняется. Отменённый ответ
приходит вызывающему с `Cancelled`, и UI его не показывает.

Ответственность:

1. **Отмена устаревших запросов.** Пользователь нажал Ctrl+Q, не дождался и выделил другой текст —
   первый запрос отменяется, его результат не покажется поверх нового. Каналы независимы:
   перевод в главном окне не отменяет всплывающий.
2. **Умный выбор языка перевода.** В настройках два языка: основной (например, `ru`) и
   дополнительный (`en`). Если исходный язык «auto» и сервис определил, что текст уже на основном
   языке, запрос автоматически повторяется с переводом на дополнительный. Так одна клавиша
   работает в обе стороны: английский → русский и русский → английский.
3. **Кэш.** LRU на 200 записей, ключ — `serviceId|src|dst|text`. Повторный перевод того же
   фрагмента мгновенный и не тратит квоту DeepL. Кэш только в памяти.
4. **Проверки до сети.** Пустой текст (`EmptyText`), превышение `maxLength` (`TooLong`),
   ненастроенный сервис (`Auth`) — ошибка возвращается сразу, без запроса (через
   `TranslationReply::createFinished`, то есть асинхронно).

### 4.10. Сеть и прокси — `NetworkManager`

Владеет единственным `QNetworkAccessManager` приложения.

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
    QNetworkRequest makeRequest(const QUrl &url) const;   // таймаут, User-Agent, HTTP/1.1
    // Проверка прокси без сохранения: временный QNAM с заданными параметрами
    void testProxy(const ProxySettings &p, const QUrl &url,
                   std::function<void(bool ok, const QString &message)> cb);
    QString describeNetworkError(QNetworkReply *reply, bool includeDetails = true) const;
};
```

Сопоставление с Qt:

| Режим / тип | Реализация |
|---|---|
| Без прокси | `QNetworkProxy::NoProxy` |
| Системный | `QNetworkProxyFactory::setUseSystemConfiguration(true)` (WinINet/WPAD, PAC) |
| HTTP | `QNetworkProxy::HttpProxy`; для https-адресов Qt сам делает туннель `CONNECT` |
| SOCKS5 | `QNetworkProxy::Socks5Proxy`; DNS-имена резолвятся на стороне прокси |

Логин и пароль задаются прямо в `QNetworkProxy`. `proxyAuthenticationRequired` не показывает
диалог: при наличии данных они передаются один раз; повторный запрос завершается отказом. Ошибки
прокси (`ProxyAuthenticationRequiredError`, `ProxyConnectionRefusedError`, `ProxyNotFoundError`,
`ProxyTimeoutError` и т.п.) превращаются в короткие сообщения «Proxy authentication failed» /
«Proxy connection failed» через `describeNetworkError`; остальные ошибки сохраняют текст Qt. Для
Cloud-URL с ключом `includeDetails = false` оставляет только общий «Network error».

После `applyProxy` вызывается `nam->clearConnectionCache()`, чтобы уже открытые keep-alive
соединения не продолжали идти мимо нового прокси. `testProxy` использует такой же временный QNAM,
те же таймаут/User-Agent и считает успехом любой HTTP-ответ (кроме `407 Proxy Authentication
Required`); в UI проверка идёт на `https://www.gstatic.com/generate_204`. Раньше использовался
`https://translate.googleapis.com/`, который отвечает `404`, и проверка ошибочно считала рабочий
прокси неисправным.

**О «HTTPS-прокси» (решение принято).** Под HTTPS-прокси в проекте понимается HTTP-прокси,
через который идут https-запросы (туннель `CONNECT`). Отдельного типа для этого нет: тип `Http`
обслуживает и http-, и https-адреса. Прокси с TLS-соединением *до самого прокси* `QNetworkProxy`
не поддерживает, и в проекте он не нужен.

SOCKS4 Qt не поддерживает; в UI предлагается только SOCKS5.

Общие параметры запросов: `setTransferTimeout(10 000 мс)` (настраивается), постоянный
`User-Agent: qtranslate/<версия>`, только HTTPS.

**HTTP/1.1 для бесплатного Google.** Qt 6.12 по умолчанию согласует HTTP/2, и
`translate.googleapis.com/translate_a/single` в наших проверках стабильно отвечал на такие запросы
`HTTP 429`, хотя те же GET/POST по HTTP/1.1 (curl, .NET) проходили. Поэтому `makeRequest()` ставит
`QNetworkRequest::Http2AllowedAttribute = false`; после этого gtx переводит без 429. Запросы ко всем
сервисам идут по HTTP/1.1 — для DeepL это не имеет значения, а поведение бесплатного Google
становится предсказуемым. Регрессия проверяется в `tst_networkmanager::requestsUseHttp11`.

**Состояние на этапе 8.** Реализованы `applyProxy`, `testProxy`, обработка авторизации/ошибок
прокси и `describeNetworkError`. Режимы `None`/`System`/`Manual`, типы HTTP/SOCKS5; после смены
прокси кэш соединений сбрасывается. Прокси применяется при старте и при сохранении настроек.
Системный режим и ручной HTTP-прокси проверены на реальном прокси `127.0.0.1:2080`.

### 4.11. Настройки — `AppSettings` и `SettingsStore`

`AppSettings` — обычная структура-значение со всеми параметрами (вложенные группы `general`,
`hotkeys`, `translation`, `google`, `deepl`, `llm`, `network`, `popup`, `mainWindow` и
`settingsVersion`). `SettingsStore` загружает и сохраняет её и сообщает об изменениях:

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
    void save(const AppSettings &s);       // пишет на диск и испускает changed
signals:
    void changed(const AppSettings &oldS, const AppSettings &newS);
};
```

Окно настроек редактирует **копию** `AppSettings`; по «ОК» отдаёт её в `save()`.
`AppController` сравнивает старое и новое значение и применяет только изменившееся
(перерегистрирует клавиши, меняет языки, автозагрузку и т.д.).

Хранение: INI-файл через `QSettings`.

- Обычный режим: `%APPDATA%\qtranslate\qtranslate.ini`.
- Портативный режим: если рядом с exe лежит `qtranslate.ini`, используется он. Путь выбирает
  чистая `SettingsStore::defaultFilePath(applicationDir, appDataDir)`, поэтому правило проверяется
  тестом без записи рядом с тестовым exe.

`organizationName` не задан (§4.1), поэтому `QSettings` по умолчанию для этого не годится.
`SettingsStore` открывает INI по явному пути — `QSettings(path, QSettings::IniFormat)`. Группа
`general` в INI называется `[app]`: `QSettings` зарезервировал имя `[General]` для ключей верхнего
уровня и иначе экранирует группу в `[%General]`.

Секреты (API-ключи, пароль прокси) хранятся зашифрованными через Windows DPAPI
(`CryptProtectData`, привязка к учётной записи пользователя) в base64. Модуль
`platform/secretprotector` предоставляет `protect(QString) → QByteArray` и
`unprotect(QByteArray) → QString`. `SettingsStore` не знает WinAPI: приложение передаёт ему
`SecretCodec` из двух `std::function`; без кодека секреты не пишутся вовсе (открытым текстом —
никогда). Если расшифровать не удалось (файл перенесён на другую машину), поле считается пустым.

Ключи файла:

| Ключ | Тип | По умолчанию |
|---|---|---|
| `app/autostart` | bool | `false` |
| `app/startMinimized` | bool | `true` |
| `app/uiLanguage` | string | `system` (`system`, `en`, `ru`, `es`, `de`, `fr`, `pt-BR`, `it`, `pl`, `uk`, `tr`, `zh-Hans`, `ja`, `ko`, `ar`, `hi`) |
| `app/restoreClipboard` | bool | `true` |
| `hotkeys/popup` | QKeySequence (portable text) | `Ctrl+Q` |
| `hotkeys/replaceSelection` | QKeySequence | `Ctrl+Shift+Q` |
| `hotkeys/mainWindow` | QKeySequence | `Ctrl+W` |
| `hotkeys/showMainWindow` | QKeySequence | пусто |
| `hotkeys/pauseInFullscreen` | bool | `true` |
| `translation/service` | string | `google` |
| `translation/sourceLang` | string | `auto` |
| `translation/primaryLang` | string | язык системы |
| `translation/secondaryLang` | string | `en` (или `ru`, если системный — английский) |
| `translation/maxLength` | int | `20000` |
| `translation/autoTranslateDelayMs` | int | `700` (0 — выкл.) |
| `google/mode` | `free` \| `cloud` | `free` |
| `google/apiKey` | секрет | пусто |
| `deepl/mode` | `api` \| `free-web` | `api`; старый `translation/service=deepl-web` мигрирует в `deepl` + `free-web` |
| `deepl/apiKey` | секрет | пусто |
| `deepl/englishVariant` | `EN-US` \| `EN-GB` | `EN-US` |
| `llm/baseUrl` | string | `https://openrouter.ai/api/v1` |
| `llm/apiKey` | секрет | пусто |
| `llm/model` | string | `openrouter/free` |
| `network/proxyMode` | `none` \| `system` \| `manual` | `system` |
| `network/proxyType` | `http` \| `socks5` | `http` |
| `network/proxyHost`, `proxyPort` | string, int | — |
| `network/proxyAuth`, `proxyUser` | bool, string | — |
| `network/proxyPassword` | секрет | — |
| `network/timeoutMs` | int | `10000` |
| `popup/width`, `popup/height`, `popup/autoCloseOnFocusLoss` | — | `460`, `260`, `true` |
| `mainWindow/geometry`, `mainWindow/state` | QByteArray | — |

В файл пишется `settingsVersion`; при изменении формата миграция выполняется в `SettingsStore::load()`.

### 4.12. Автозагрузка — `Autostart`

```cpp
namespace Autostart {
    QString commandForExecutable(const QString &path);
    bool isEnabled();
    bool setEnabled(bool on);   // false — нет доступа к реестру
    bool sync(bool wanted);     // привести реестр к настройке, обновить путь
    bool isApproved();          // учитывает StartupApproved
}
```

- Запись в `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, значение `qtranslate` =
  `"C:\…\qtranslate.exe" --minimized` (через `QSettings` с `NativeFormat`). Права администратора
  не нужны.
- `sync()` вызывается при каждом старте: если автозагрузка включена, а путь к exe изменился
  (программу переместили или обновили в другую папку), запись обновляется.
- Пользователь может отключить автозагрузку в Диспетчере задач — тогда Windows ставит флаг в
  `…\Explorer\StartupApproved\Run`. Мы его не переписываем, а только показываем в настройках
  «отключено в Диспетчере задач».

### 4.13. Пользовательский интерфейс

#### `TrayIcon`

`QSystemTrayIcon` с меню:

```
Показать окно
Сервис ▸  ● Google Translate
          ○ DeepL
───────────
Настройки…
Выход
```

- Левый клик — показать/скрыть главное окно.
- Подсказка значка: «qtranslate — Ctrl+Q / Ctrl+W» (актуальные сочетания).
- `showMessage()` для уведомлений: занятая горячая клавиша, ошибка сети при выключенном окне,
  первый запуск.
- Если `QSystemTrayIcon::isSystemTrayAvailable()` вернул `false`, главное окно всегда показывается
  при старте, а закрытие окна завершает программу.

#### `PopupWindow` (Ctrl+Q)

```
┌──────────────────────────────────────────────┐
│ EN → RU ▾                    Google   📌  ✕   │
├──────────────────────────────────────────────┤
│ Перевод текста, который можно выделять       │
│ и копировать…                                │
├──────────────────────────────────────────────┤
│ ⧉ Копировать   ⇄ В главное окно              │
└──────────────────────────────────────────────┘
```

- Флаги: `Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint` — без кнопки на панели задач.
- Создаётся лениво при первом Ctrl+Q и переиспользуется, чтобы появляться без задержки.
- Позиция: у курсора в момент нажатия клавиши (точка запоминается до захвата выделения), с
  ограничением рамками `QScreen::availableGeometry()` того монитора, где курсор. Чистая геометрия
  вынесена в `core/popupplacement`: окно сначала ставится справа-снизу, у края отражается
  влево/вверх, затем прижимается к экрану.
- Ширина 460 логических пикселей (настройка — этап 5). Высота подстраивается под текст результата
  до предела `min(600, 70% рабочей области)`; дальше прокрутка.
- Состояния: «загрузка» (анимированный спиннер, показ сразу после захвата выделения), «результат»,
  «ошибка» (текст и кнопка «Повторить»), «нет выделения» (короткая подсказка, закрывается через
  1,5 с).
- Закрытие: Esc и ✕ всегда; потеря фокуса — если не закреплено 📌. Закрепление действует в пределах
  одного вызова и сбрасывается при новом Ctrl+Q. Меню языка — временный popup: пока приложение
  остаётся активным и другое наше окно не стало активным, закрытие меню не закрывает окно; клик в
  другой программе или в главном окне закрывает. Настройка `popup/autoCloseOnFocusLoss` появится на
  этапе 5, пока поведение включено.
- Клик по «Auto → Русский ▾» открывает меню целевых языков; выбор сразу переводит сохранённый
  исходный текст заново. После результата в строке показывается определённый язык.
- «Копировать» кладёт перевод в буфер. «В главное окно» испускает
  `openInMainWindowRequested(request, result)`; `AppController` передаёт оба текста в
  `MainWindow::showTranslation()` без нового запроса и показывает главное окно.
- Скрытие испускает `closed()`; `AppController` отменяет незавершённый запрос канала `Popup`, чтобы
  поздний ответ не показался поверх следующего. Новый Ctrl+Q заменяет старый запрос того же канала.
- Активация: Windows разрешает процессу, получившему `WM_HOTKEY`, вывести окно на передний план,
  поэтому `activateWindow()` после горячей клавиши срабатывает.

#### `MainWindow` (Ctrl+W)

```
┌───────────────────────────────────────────────────────────┐
│ [Авто ▾]  ⇄  [Русский ▾]          Сервис: [Google ▾]       │
├─────────────────────────────┬─────────────────────────────┤
│ Исходный текст              │ Перевод (только чтение)     │
│                             │                             │
├─────────────────────────────┴─────────────────────────────┤
│ Определён: английский   [Перевести Ctrl+Enter] [Копировать]│
└───────────────────────────────────────────────────────────┘
```

- Два `QPlainTextEdit`: исходный текст и перевод (read-only).
- Автоперевод при наборе с задержкой `autoTranslateDelayMs` (0 — выкл.); «Перевести» и Ctrl+Enter —
  перевести сразу. Повторное нажатие до ответа заменяет запрос (канал `MainWindow`, §4.9).
- ⇄ меняет языки местами, переносит перевод в поле исходного текста и сразу переводит его. Если
  исходный язык «авто», новым целевым становится определённый язык последнего перевода; пока он
  неизвестен, кнопка недоступна.
- Строка состояния: «Translating…», определённый язык (когда исходный — «авто»), ошибки (красным,
  текст из `ui/translationerrortext`). «Копировать» доступна, когда есть перевод.
- Выбор сервиса показывает активный сервис и доступен, только когда сервисов больше одного (этап 6).
- Окно не знает о сервисах и сети: испускает `translateRequested(request)`, а `AppController`
  вызывает `TranslationManager` и возвращает итог через `showTranslationStarted()` /
  `showResult(result)`; результат показывается, только если это текущий запрос окна.
- `translateText(text)` подставляет текст, полученный сценарием Ctrl+W, в поле исходного текста,
  ставит курсор в начало и запускает перевод.
- `showTranslation(request, result)` показывает перевод, переданный из всплывающего окна: выставляет
  языки, оба текста и статус, но не запускает новый запрос.
- `closeEvent` скрывает окно в трей; выход — только из меню трея.
- Геометрия сохраняется в настройках.

#### `LanguageComboBox`

`QComboBox` с внутренними кодами языков: `setLanguages(codes, includeAuto)` (сортировка по
названию, «Auto-detect» сверху, текущий язык сохраняется, если он остался в списке),
`currentCode()` / `setCurrentCode()`, сигнал `currentCodeChanged`. Подстановка ближайшего языка
при смене сервиса (§4.8) добавляется на этапе 6.

#### `SettingsDialog`

Модальный диалог редактирует копию `AppSettings`; «ОК» возвращает её, `AppController` сохраняет и
применяет изменения, «Отмена» ничего не меняет. Вкладки:

1. **Общие** — автозагрузка (с подсказкой, если Windows отключила её в Диспетчере задач), запуск
   свёрнутым, язык интерфейса (системный/English/Русский), восстановление буфера обмена, закрытие
   popup при потере фокуса.
2. **Горячие клавиши** — `QKeySequenceEdit` для Ctrl+Q, Ctrl+Shift+Q (перевод с заменой
   выделения), Ctrl+W и показа главного окна. Пустое поле отключает действие. Перед принятием
   проверяются односоставность, поддержка Win32 и отсутствие дубликатов. Пока фокус в любом поле,
   диалог испускает `hotkeyEditingChanged(true)`, и `AppController` приостанавливает глобальные
   клавиши; после ухода фокуса/закрытия — возобновляет.
3. **Перевод** — исходный язык (с «Auto-detect»), основной и дополнительный языки, максимальная
   длина, задержка автоперевода. Дополнительный язык используется для умного переключения,
   когда текст уже на основном языке (этап 8).
4. **Сервисы** — Google: режим (бесплатный/Cloud API), ключ (password echo). DeepL: режим
   (API с ключом или бесплатный веб-эндпоинт без ключа), ключ (password echo), вариант английского,
   кнопка «Проверить ключ» с асинхронным результатом (`character_count`/`character_limit` или
   ошибка). В режиме `free-web` поле ключа и проверка блокируются, показывается предупреждение о
   неофициальности; отдельной группы «DeepL (free web)» нет. Кнопка проверки не блокирует диалог;
   ключ берётся из поля ввода. Ключи сохраняются через DPAPI из этапа 5.
5. **Сеть** — режим (без прокси/системный/ручной), тип HTTP/SOCKS5, хост, порт, авторизация,
   логин, пароль (password echo), таймаут, кнопка «Проверить соединение» с асинхронным результатом.
   Поля включаются только для ручного режима; диалог испускает `proxyTestRequested()`, а
   `AppController` проверяет ещё не сохранённые параметры через `NetworkManager::testProxy`.

#### `AppIcon`

Иконки для трея и окна. Для трея — отдельные варианты под светлую и тёмную тему панели задач
(`QStyleHints::colorScheme()`), во время запроса можно показывать анимированный индикатор.
Иконка exe (`resources/icons/app.ico`) и версия встраиваются через `resources/qtranslate.rc`.

#### Локализация

Строки помечены `tr()`/`QT_TRANSLATE_NOOP`. Английский — исходный язык; в `i18n/` лежат
`qtranslate_ru_RU.ts` и переводы на испанский, немецкий, французский, португальский (Бразилия),
итальянский, польский, украинский, турецкий, китайский (упрощённый), японский, корейский, арабский
и хинди. Все `.ts` компилируются `qt_add_translations` и встраиваются в ресурсы `:/i18n`.

`core/uilanguages` хранит поддерживаемые коды, родные названия и путь к `.qm`. `normalize()`
разрешает `system`, региональные теги (`de-DE`, `ru_RU`) и особые случаи: `pt-PT` → `pt-BR`,
`zh-CN`/`zh-Hans` → `zh-Hans`; неподдерживаемые языки и `zh-Hant` пока откатываются на английский.
`AppController::applyUiLanguage()` загружает нужный `QTranslator`, для арабского включает
`Qt::RightToLeft` и рассылает `LanguageChange`; главное окно переприменяет статические тексты, трей
перестраивает меню, popup пересоздаётся при следующем вызове.

`tst_uilanguages` проверяет нормализацию кодов и таблицу языков, `tst_translations` — полноту всех
`.ts`, сохранность `%1`/`%2`, эмодзи и краевых пробелов. Переводы — машинно-ассистированный первый
проход; перед публичным релизом желательна проверка носителями языка, особенно для арабского RTL и
длинных строк в настройках.

Стандартные диалоги Qt переводятся отдельными `qt_<locale>.qm`; `AppController::applyUiLanguage()`
загружает их из `../translations` рядом с exe (в dev-сборке — из
`QLibraryInfo::TranslationsPath`). `windeployqt --translations` развёртывает каталоги для
`ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar`; в Qt 6.12 нет `qt_hi.qm`, поэтому стандартные
кнопки и меню для хинди остаются английскими.

### 4.14. Переносимость и план поддержки Linux

Windows-код изолирован: заголовки `platform/*.h` не включают `<windows.h>`, а реализации —
`*_win.cpp`; для остальных платформ есть `*_stub.cpp`. `qtranslate_core`, `net/`, `services/` и
UI не используют WinAPI. Благодаря этому `CMakeLists.txt` рассчитан на сборку каркаса под Linux,
но фактическая проверка и реальные платформенные действия появятся только на этапе 11:
сейчас все Linux-реализации — заглушки.

| Модуль | Windows (сейчас) | Linux X11 (план) | Linux Wayland (план) |
|---|---|---|---|
| Горячие клавиши | `RegisterHotKey` | `XGrabKey` через xcb/Xlib | портал `org.freedesktop.portal.GlobalShortcuts`; если нет — команды CLI, привязанные в DE |
| Захват выделения | `SendInput` Ctrl+C | XTest + `QClipboard` | синтетический ввод недоступен; пользователь копирует сам |
| Вставка перевода | `SendInput` Ctrl+V | XTest Ctrl+V | недоступна надёжно; максимум — `wtype`/`ydotool` как внешняя зависимость |
| Активное окно | `GetForegroundWindow` | `_NET_ACTIVE_WINDOW`/`XGetInputFocus` | нет надёжного API; режим «в поле» не гарантируется |
| Автозагрузка | реестр `Run` | `~/.config/autostart/qtranslate.desktop` | то же |
| Секреты | DPAPI | libsecret | libsecret; fallback — файл 0600 + предупреждение |
| Трей | `QSystemTrayIcon`/WinAPI | StatusNotifierItem | StatusNotifierItem; в GNOME нужен AppIndicator-extension |
| Настройки | `%APPDATA%\qtranslate` | `$XDG_CONFIG_HOME/qtranslate` | то же |
| Логи | `%LOCALAPPDATA%\qtranslate` | `$XDG_STATE_HOME/qtranslate` | то же |

**Команды CLI.** Чтобы горячие клавиши работали в любом окружении (и на Wayland, где портала
может не быть), приложение получит команды `--translate-popup`, `--translate-main` и
`--translate-inplace`. Их можно назначить на сочетания в настройках GNOME/KDE/Xfce; для
X11-реализации они не нужны, но остаются удобным fallback и используются тестами.

**Трей.** Qt использует StatusNotifierItem/DBus. В GNOME по умолчанию иконки приложений не
показываются — потребуется расширение AppIndicator; если трей недоступен, приложение должно
показать главное окно и предупредить, а не завершаться молча.

**Секреты.** Предпочтительный вариант — libsecret (`secret_password_store_sync`), который
использует системный keyring. Если библиотеки нет при сборке, секреты пишутся в INI с правами
0600, а в настройках показывается предупреждение. Молчаливое хранение паролей в открытом виде
недопустимо.

**Сборка и пакеты.** CMake уже умеет `install` и `qt_generate_deploy_app_script`; для Linux
добавляются `.desktop`, иконки hicolor, `share/applications` и `RPATH`/`$ORIGIN` для AppImage.
Планируемые артефакты: `qtranslate-<версия>-linux-x86_64.tar.gz` (портативный, с Qt),
`.deb` (зависимости Qt из дистрибутива) и AppImage. CI — `ubuntu-24.04`, Qt 6.5+, тесты в
`xvfb-run`; платформенные тесты Windows (`tst_hotkeymanager`, `tst_autostart`,
`tst_secretprotector`) заменяются Linux-аналогами, остальные 14 тестов общие.

**Ограничения Wayland.** Глобальные сочетания и синтетический ввод — не базовые возможности
Wayland. Портал `GlobalShortcuts` поддерживается не всеми рабочими столами и версиями; вставка
в активное поле и Ctrl+Shift+Q на Wayland не могут быть гарантированы без внешних инструментов
или портала RemoteDesktop. Поэтому на первом этапе Linux-поддержки X11 считается полноценным
таргетом, а Wayland — ограниченным: Ctrl+Q с копированием вручную, CLI-команды и явные
предупреждения в интерфейсе. macOS не планируется.

### 4.15. Проверка орфографии, грамматики и пунктуации (план)

Задача на будущее: отдельный режим проверки текста, не связанный с переводом. Полный дизайн,
провайдеры и критерии — [`proofreading.md`](proofreading.md).

- Отдельный `ProofreadingService` и `ProofreadingIssue` с `offset`/`length` в UTF-16,
  `message`, `replacements`, `ruleId`, `category`, `severity`.
- Провайдеры: LanguageTool (HTTP/self-hosted, грамматика и пунктуация), Hunspell/Nuspell
  (только орфография), Yandex Speller (орфография ru/uk/en), LLM (опционально, через diff).
- Отдельная вкладка настроек «Проверка текста», независимые ключи `proofreading/*`; ключи API —
  DPAPI. `checkBeforeTranslation=false` и `autoReplace=false` по умолчанию.
- Замена в главном окне — через `QTextDocument` с undo; во внешнем приложении — через механизм
  Ctrl+Shift+Q с проверкой активного окна и подтверждением.
- Текст не логируется; публичные API имеют лимиты, для регулярного использования — свой
  LanguageTool-сервер.

### 4.16. LLM-действия и словарь (частично реализовано)

Перевод через OpenAI-совместимый провайдер уже реализован (§4.7.2): сервис `llm`, настройки
`[llm]`, транспорт `src/ai/openaichat.*`. Остальные действия — задача на будущее; полный дизайн,
провайдеры и критерии — [`llm-actions.md`](llm-actions.md).

- `LlmProvider` — тот же OpenAI-совместимый транспорт для облака (OpenAI, OpenRouter,
  Gemini-compatible) и локальных серверов (Ollama, LM Studio, llama.cpp).
- `AiAction`/`AiActionRegistry` — действия `translate`, `proofread`, `summarize`, `rewrite`,
  `explain`, `pronounce`; промпты с фиксированной ролью, низкой температурой и валидацией.
  `translate` уже работает как обычный `TranslationService`, остальные появятся в отдельной
  панели с предпросмотром.
- Словарь — отдельный `DictionaryService`, не `TranslationService`; источники: LLM (JSON-схема),
  Google `dt=bd` для коротких текстов, локальный CSV-глоссарий, Wiktionary. Дизайн —
  [`dictionary.md`](dictionary.md).
- Настройки — отдельная вкладка «AI»: base URL, модель, ключ (DPAPI), таймаут, проверка
  соединения; ключи `llm/*`. Словарь использует те же `[llm]` настройки, но имеет собственный
  кэш и не влияет на перевод.
- Результат показывается в предпросмотре/diff; замена текста — только по подтверждению.
- LLM не заменяет DeepL/Google/LanguageTool по умолчанию; ASR/TTS/OCR — отдельные будущие задачи.

---

## 5. Основные сценарии

### 5.1. Запуск

```
main
 ├─ QApplication, setQuitOnLastWindowClosed(false)
 ├─ SingleInstance: уже запущены? → отправить "activate" → exit(0)
 ├─ SettingsStore: DPAPI SecretCodec, load()   (миграция, расшифровка секретов)
 ├─ QTranslator по general/uiLanguage
 └─ AppController
     ├─ NetworkManager (timeout, applyProxy), GoogleTranslateService + DeepLService → TranslationManager
     ├─ языки/сервис/maxLength/restoreClipboard из настроек
     ├─ MainWindow + восстановление геометрии; PopupWindow — лениво при первом Ctrl+Q
     ├─ TrayIcon.show()
     ├─ HotkeyManager.register(...)  → занятые сочетания → уведомление в трее
     ├─ Autostart::sync(settings.autostart)
     └─ если нет --minimized и не startMinimized → MainWindow.show()
```

Всё, что не нужно для появления значка в трее, создаётся лениво (окно настроек, всплывающее окно).

### 5.2. Ctrl+Q — перевод во всплывающем окне

```
Пользователь      HotkeyMgr     AppController     SelectionGrabber   TranslationMgr   PopupWindow
    │ Ctrl+Q          │                │                  │                 │               │
    ├────────────────►│ WM_HOTKEY      │                  │                 │               │
    │                 ├─activated────► │ pos = cursor     │                 │               │
    │                 │                ├─grab()─────────► │ ждём отпускания │               │
    │                 │                │                  │ снимок буфера   │               │
    │                 │                │                  │ SendInput Ctrl+C│               │
    │                 │                │                  │ ждём seq number │               │
    │                 │                │                  │ читаем, восстан.│               │
    │                 │                │◄──cb(text)───────┤                 │               │
    │                 │                │ text пуст? → showNoSelection(pos) ─────────────────► │
    │                 │                ├─showLoading(pos)────────────────────────────────────►│
    │                 │                ├─translate(req, Popup)───────────► │ отменить старый│
    │                 │                │                  │                 │ кэш? → сразу   │
    │                 │                │                  │                 │ сервис → HTTP  │
    │                 │                │◄────────────── reply.finished ────┤                 │
    │                 │                ├─showResult / showError────────────────────────────► │
```

### 5.3. Ctrl+W — перевод в главном окне

Отличается от Ctrl+Q:

- `AppController` сначала запускает `SelectionGrabber::grab()`, **не показывая окно**: если показать
  и активировать его раньше, Ctrl+C уйдёт в собственное окно, а не в программу с выделением.
- После колбэка окно показывается и активируется, исходный текст подставляется в левое поле,
  перевод — в правое. Перевод стартует сразу, не дожидаясь показа: окно появляется ещё до ответа
  сервиса.
- Если выделения нет, окно просто открывается с фокусом в поле ввода — это удобный способ
  «вызвать переводчик и набрать текст».
- Повторное срабатывание Ctrl+W во время захвата игнорируется, чтобы не перехватить фокус у
  source-программы.

### 5.4. Ctrl+Shift+Q — перевод с заменой выделения

```
Пользователь   HotkeyMgr   AppController   SelectionGrabber   TranslationMgr   Windows
  │Ctrl+Shift+Q │              │                 │                │              │
  ├────────────►│ WM_HOTKEY    │                 │                │              │
  │             ├─activated──► │ target = foregroundWindow()      │              │
  │             │              ├─grab()──────────►│ снимок буфера, Ctrl+C,       │
  │             │              │◄──text───────────┤ восстановление буфера        │
  │             │              ├─translate(req, InPlace)──────────►│              │
  │             │              │◄──────────── finished ───────────┤              │
  │             │              ├─target всё ещё активен? ────────────────────────►│
  │             │              │   да: буфер := перевод, SendInput Ctrl+V,        │
  │             │              │       через 500 мс вернуть буфер                 │
  │             │              │   нет: перевод в буфер + уведомление в трее      │
```

- Целевое окно (`GetForegroundWindow`) запоминается **до** захвата. Своё окно не показывается,
  поэтому фокус и выделение остаются в поле ввода; после ответа Ctrl+V заменяет выделение.
- Перевод идёт через тот же `TranslationManager`, что и Ctrl+Q, с тем же умным выбором языка:
  при основном русском и дополнительном английском русский ответ автоматически переводится на
  английский. Канал `InPlace` не отменяет запросы popup и главного окна.
- Если пользователь переключился в другое окно, пока шёл запрос, перевод **не вставляется**:
  он копируется в буфер обмена, а в трее показывается уведомление. Это защищает от вставки в
  чужое поле (в том числе в поле пароля).
- Буфер обмена восстанавливается через 500 мс после Ctrl+V. Если `app/restoreClipboard` выключен,
  перевод остаётся в буфере.
- Нет выделения или ошибка перевода → уведомление в трее; окно не появляется.

### 5.5. Применение настроек

```
SettingsDialog ──OK──► SettingsStore::save ──changed(old,new)──► AppController
                                                                    ├─ hotkeys изменились → register/unregister только их
                                                                    ├─ autostart         → Autostart::sync
                                                                    ├─ языки/сервис      → MainWindow/PopupWindow + TranslationManager
                                                                    ├─ maxLength         → TranslationManager + SelectionGrabber
                                                                    ├─ restoreClipboard  → SelectionGrabber
                                                                    ├─ network           → NetworkManager.applyProxy / setTimeoutMs
                                                                    ├─ popup width/focus → PopupWindow
                                                                    ├─ uiLanguage       → QTranslator + LanguageChange
                                                                    └─ service/ключи     → service
```

Пока фокус находится в любом `QKeySequenceEdit`, `SettingsDialog` испускает
`hotkeyEditingChanged(true)`, и `AppController` приостанавливает глобальные клавиши
(`HotkeyManager::setSuspended`). После ухода фокуса или закрытия диалога регистрация
восстанавливается.

Смена сервиса в трее или главном окне вызывает `TranslationManager::setActiveService()`.
`activeServiceChanged` → `AppController::updateLanguageConfiguration()`: списки языков обоих окон
обновляются, неподдерживаемый язык заменяется через `Languages::closest`; выбор сохраняется в
`translation/service` через `SettingsStore::save()`.

### 5.6. Завершение

«Выход» в трее → `AppController::prepareForQuit()`: флаг завершения (чтобы `closeEvent` не прятал
окно и `changed` не применялся повторно), сохранение геометрии главного окна в `SettingsStore`,
`HotkeyManager::unregisterAll()`, отмена активных запросов, скрытие popup → `QApplication::quit()`.
Повторный вызов (`aboutToQuit`, завершение сеанса) ничего не делает.

---

## 6. Обработка ошибок

| Ситуация | Где обрабатывается | Что видит пользователь |
|---|---|---|
| Горячая клавиша занята | `AppController` при регистрации | Уведомление в трее, предупреждение в настройках |
| Нет выделения | `AppController` | «Ничего не выделено» у курсора / пустое главное окно |
| Нет сети, ошибка прокси, TLS | сервис → `Network` | Текст ошибки и «Повторить» в окне перевода |
| Таймаут | `Timeout` | «Сервис не ответил за N с» |
| Пустой текст | `TranslationManager` до запроса → `EmptyText` | «Нечего переводить» |
| Неверный ключ | `Auth` | «Неверный API-ключ DeepL» + ссылка «Открыть настройки» |
| Квота / частые запросы | `QuotaExceeded`, `RateLimited` | Пояснение и совет сменить сервис |
| Неожиданный формат ответа | парсер → `BadResponse` | «Сервис вернул неожиданный ответ», подробности в лог |
| Сервис не настроен | `TranslationManager` до запроса | «Для DeepL нужен API-ключ» + «Открыть настройки» |

Исключения C++ не используются для управления потоком: ошибки передаются в `TranslationResult`.

---

## 7. Логирование, безопасность, приватность

- Категории `QLoggingCategory`: `qtranslate.app`, `.hotkey`, `.grab`, `.net`, `.translation`
  (`TranslationManager`), `.service.google`, `.service.deepl`, `.settings`, `.autostart`, `.secret`.
  По умолчанию выводятся только предупреждения; подробный лог включается
  переменной окружения `QT_LOGGING_RULES` или флагом `--verbose` (запись в
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`, ротация при 5 МБ в `.1`). Обработчик сообщений
  пишет время, уровень, категорию и текст; каталог создаётся при старте.
- **В лог не пишутся переводимые тексты, ключи и пароли** — только длины, коды ответов, время.
- Все запросы только по HTTPS; ошибки TLS не игнорируются.
- Секреты в INI зашифрованы DPAPI (§4.11).
- В справке и при первом выборе сервиса указать, что текст отправляется стороннему сервису
  (Google/DeepL).

---

## 8. Тестирование

Модульные тесты (Qt Test; логика берётся из `qtranslate_core`, платформенные модули
подключаются к отдельным тестовым целям):

| Тест | Что проверяет |
|---|---|
| `tst_googleparser` | Разбор ответов gtx и Cloud API по фикстурам: несколько предложений, определение языка, пустой/битый ответ, коды ошибок Cloud с телом |
| `tst_googletranslateservice` | Сервис против локального HTTP-сервера: free/cloud режимы, тело/ключ, разбиение длинного текста и склейка, HTTP-ошибки, отказ в соединении, таймаут, отмена |
| `tst_deeplparser` | Разбор `translations[]`, почастный разбор, `/v2/usage`, коды ошибок → `TranslationError` |
| `tst_deeplservice` | DeepL против локального HTTP-сервера: Authorization, массив `text`, варианты английского, Developer/Growth хосты, ошибки, таймаут, проверка ключа |
| `tst_deeplwebparser` | Разбор ответа веб-эндпоинта DeepL, коды ошибок `403/429/413/5xx`, распознавание rate limit в теле |
| `tst_deeplwebservice` | DeepL free web против локального HTTP-сервера: `Authorization: None`, коды языков, 5000 символов, повтор на `429`, сериализация и пауза 2 с, ошибки, таймаут |
| `tst_languages` | Маппинг кодов в обе стороны, варианты `EN-US/EN-GB`, `PT-BR`, `deepl-web`, `closest` при смене сервиса, неизвестные коды |
| `tst_networkmanager` | Типы прокси, HTTP-прокси с авторизацией и `CONNECT`, SOCKS5 с логином/паролем, `testProxy`, понятные ошибки и отсутствие пароля в тексте |
| `tst_popupplacement` | Позиционирование окна Ctrl+Q: отражение у краёв, прижатие к экрану, второй монитор с отрицательными координатами |
| `tst_popupwindow` | Состояния всплывающего окна (offscreen): запрос, результат, копирование, ошибка/«Повторить», смена языка, закрепление, автозакрытие подсказки |
| `tst_textchunker` | Разбиение по абзацам/предложениям, слова длиннее лимита, суррогатные пары не разрываются |
| `tst_hotkeysequence` | `QKeySequence` ↔ `(MOD, VK)`, неподдерживаемые клавиши |
| `tst_hotkeymanager` | `RegisterHotKey`: конфликт сочетаний, `setSuspended`, отложенная регистрация (Windows; собирается вместе с `platform/hotkeymanager_win.cpp`) |
| `tst_settings` | Значения по умолчанию, запись/чтение, миграция версий, портативный путь, DPAPI-совместимое шифрование секретов |
| `tst_settingsdialog` | Заполнение вкладок (включая ключи/режим/вариант), возврат изменённой копии, проверка сочетаний и языков, приостановка глобальных клавиш, запрос проверки ключа (offscreen) |
| `tst_secretprotector` | DPAPI: round-trip, пустые и испорченные данные |
| `tst_autostart` | Формат команды автозагрузки |
| `tst_translationmanager` | Отмена запросов в канале, кэш, повтор на дополнительный язык — на фейковом сервисе |
| `tst_uilanguages` | Поддерживаемые языки интерфейса, нормализация `system`/региональных тегов, ресурсы `.qm` |
| `tst_translations` | Полнота всех `.ts`, сохранность `%1`/`%2`, эмодзи и краевых пробелов |

Сервисы тестируются через подмену сети: фейковый `TranslationService` для менеджера и
локальный HTTP-сервер (`QTcpServer`) с заранее заданными ответами для проверки кодов ошибок,
таймаутов и прокси.

Ручной чек-лист перед релизом: захват выделения в Блокноте, браузере, Word, VS Code, Telegram;
окно в администраторском процессе (ожидаемо не работает); буфер обмена после перевода совпадает с
исходным; Ctrl+Q у краёв экрана и на двух мониторах с разным масштабом; Ctrl+Shift+Q в поле ввода
(замена выделения, восстановление буфера, защита при смене окна); работа через HTTP- и
SOCKS5-прокси с авторизацией; автозапуск после перезагрузки.

### 8.1. Результаты финальной проверки (этап 8)

Проверено вручную на Release (Windows 11, один монитор 3840×2160, масштаб 175%):

- язык интерфейса переключается English ↔ Русский в настройках без перезапуска: главное окно,
  трей и его меню обновляются сразу, выбор сохраняется в INI;
- умный язык на реальном Google: русский текст при целевом «русский» автоматически переводится на
  английский (в логе два запроса `ru → ru` и `ru → en`), статус «Detected language: Russian»;
- кэш: повторный перевод того же текста занимает ~3 мс, в логе `Cache hit`, новых сетевых запросов
  нет; ошибки не кэшируются (три ответа 429 подряд не создали записей);
- DPI: `GetProcessDpiAwareness` = 2 (per-monitor), контекст окна сравнивается с
  `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`; иконка exe и `WM_GETICON` окна совпадают с
  `resources/icons/app.ico`;
- системный режим и ручной HTTP-прокси (реальный прокси `127.0.0.1:2080`): перевод проходит;
- Ctrl+Q в Блокноте и Edge: окно показывает перевод, буфер обмена восстанавливается;
  Ctrl+W переносит исходник и перевод в главное окно, буфер также восстанавливается;
- Ctrl+Shift+Q в textarea Edge: выделенный русский текст заменён английским переводом, буфер
  обмена восстановлен; при переключении в другое окно во время запроса вставки не произошло,
  перевод остался в буфере и было показано уведомление;
- Ctrl+Q у правого-нижнего края экрана остаётся в рабочей области.

Ограничения среды: Word и VS Code не установлены; Telegram установлен и авторизован, но выделение
в личных чатах не проверялось; второго монитора с другим масштабом нет; чистая Windows без Qt
недоступна (проверен запуск `build/dist/bin/qtranslate.exe` без Qt в `PATH`); реальная перезагрузка
для автозагрузки и прокси с авторизацией не проверялись; см. §9.2.

### 8.2. CI и проверка релиза 0.9.1

v0.9.0 — первый публичный release candidate; v0.9.1 — патч с галочками установщика и
обновлённой документацией.

Локальная проверка (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug и Release: сборка без предупреждений `/W4`, **17/17 тестов**;
- `package.ps1` собрал `qtranslate-0.9.1-win64.zip` (22,4 МБ),
  `qtranslate-0.9.1-win64-setup.exe` (16,6 МБ) и `SHA256SUMS.txt`; обе контрольные суммы
  совпали с файлами;
- установщик: тихая установка в отдельный каталог, `FileVersion` exe = 0.9.1 (`qtranslate
  --version` → `qtranslate 0.9.1`), ярлык в меню «Пуск» создан; запуск `bin\qtranslate.exe`
  без Qt в `PATH` успешен, `Qt6Core.dll` и `vcruntime140.dll` загрузились из каталога установки;
- `AppMutex`: при запущенном qtranslate деинсталлятор завершился с кодом 1 и не удалил файлы;
- задачи установщика: по умолчанию меню «Пуск» и автозагрузка включены, рабочий стол выключен;
  установка с `/TASKS=desktopicon` выключает меню и автозагрузку; обновление с
  `autostart=false` не включает её обратно; при удалении запись `Run` удаляется;
- удаление: exe и ярлык удалены, каталог установки очищен, `%APPDATA%\qtranslate\qtranslate.ini`
  сохранён;
- портативный zip: распакован в отдельный каталог, `bin\qtranslate.exe` запустился без Qt в
  `PATH`, Qt и CRT загрузились из архива.

Артефакты CI (Qt 6.8.3 LTS, `windows-latest`) скачаны и проверены отдельно: zip 22,5 МБ,
установщик 16,4 МБ; установка, запуск без Qt в `PATH`, удаление и контрольные суммы — в порядке.
CI: workflow `.github/workflows/build.yml` собирает и тестирует Release на `windows-latest`,
затем повторяет упаковку; по тегу `v0.9.1` публикует релиз.

- workflow: https://github.com/youkaicat/QTranslate/actions/workflows/build.yml
- успешный прогон тега: https://github.com/youkaicat/QTranslate/actions/runs/41908976474
- релиз: https://github.com/youkaicat/QTranslate/releases/tag/v0.9.1

Файлы, скачанные из релиза, совпали с `SHA256SUMS.txt`.

### 8.3. CI и проверка релиза 0.10.0

Локальная проверка (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug и Release: сборка без предупреждений `/W4`, **21/21 тест**;
- `package.ps1` собрал `qtranslate-0.10.0-win64-setup.exe` (11,51 МБ),
  `qtranslate-0.10.0-win64.zip` (14,58 МБ) и `SHA256SUMS.txt`; контрольные суммы проверены;
- `build/dist` — 35,0 МБ; в ZIP 41 запись, 13 Qt-переводов, `opengl32sw.dll` отсутствует;
- установщик: тихая установка во временный каталог, `FileVersion`/`ProductVersion` = 0.10.0,
  запуск `bin\qtranslate.exe` без Qt в `PATH`, удаление; `%APPDATA%\qtranslate\qtranslate.ini`
  и `HKCU\...\Run` восстановлены после проверки;
- живой smoke DeepL free web через собранный сервис: `Hello, world` → `Hallo, Welt`,
  `detected_source_language=en`;
- CI (Qt 6.8.3 LTS): setup 10,63 МБ, ZIP 13,12 МБ; хэши совпали с `SHA256SUMS.txt`;
  CI-установщик проверен тихой установкой, запуском и удалением;
- CI-прогон тега: https://github.com/youkaicat/QTranslate/actions/runs/41930568365
- релиз: https://github.com/youkaicat/QTranslate/releases/tag/v0.10.0
- старые теги `v0.9.0`/`v0.9.1` не изменяются.

### 8.4. CI и проверка релиза 0.11.0

Локальная проверка (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug и Release: сборка без предупреждений `/W4`, **23/23 теста**;
- `package.ps1` собрал `qtranslate-0.11.0-win64-setup.exe` (12 097 066 байт),
  `qtranslate-0.11.0-win64.zip` (15 333 372 байта) и `SHA256SUMS.txt`;
- `build/dist` — 36 845 376 байт; в ZIP 41 запись, 13 Qt-переводов;
- установщик: тихая установка во временный каталог, `ProductName=QTranslate`,
  `FileVersion`/`ProductVersion` = 0.11.0, запуск без Qt в `PATH`, удаление; Run-запись
  автозагрузки создаётся и удаляется, `%APPDATA%\qtranslate\qtranslate.ini` и `HKCU\...\Run`
  восстановлены после проверки;
- живой smoke OpenRouter через SOCKS5 `127.0.0.1:2080`: `Hello, world` → `Привет, мир`,
  `openrouter/free`, HTTP 200;
- OpenAI-совместимый провайдер работает с настраиваемым base URL, ключом и моделью; DeepL free web
  объединён в режим `free-web` одного сервиса DeepL;
- CI (Qt 6.8.3 LTS): setup 11 173 822 байта (10,66 МБ), ZIP 13 795 207 байт (13,16 МБ);
  хэши совпали с `SHA256SUMS.txt`; CI-установщик проверен тихой установкой, запуском без Qt в
  `PATH` и удалением; `ProductName=QTranslate`, `ProductVersion=0.11.0`;
- CI-прогон тега: https://github.com/youkaicat/QTranslate/actions/runs/41943741295
- CI-прогон `main`: https://github.com/youkaicat/QTranslate/actions/runs/41943729918
- релиз: https://github.com/youkaicat/QTranslate/releases/tag/v0.11.0

---

## 9. Принятые решения и развитие

### 9.1. Принятые решения

| Вопрос | Решение |
|---|---|
| Ctrl+Q / Ctrl+W перехватываются у всех программ | Оставить по умолчанию, без предупреждений; меняются в настройках (§4.3) |
| Ctrl+Alt на не-английской раскладке | Qt может вернуть `GroupSwitchModifier` (AltGr); нормализуем его в `MOD_CONTROL \| MOD_ALT`, иначе сочетание ошибочно отвергалось бы (§4.3) |
| HTTPS-прокси | HTTP-прокси с туннелем `CONNECT`; TLS до прокси не поддерживается (§4.10) |
| Окна процессов, запущенных от администратора | Захват выделения в них не работает — это известное ограничение, его не обходим (§4.4) |
| Закрепление 📌 во всплывающем окне | Действует до закрытия окна; новый Ctrl+Q начинает с незакреплённого состояния (§4.13) |
| HTTP/2 и бесплатный Google | Принудительно HTTP/1.1: gtx стабильно отвечал `429` на HTTP/2-запросы Qt, по HTTP/1.1 работает (§4.10) |
| Перевод с заменой выделения | Отдельное действие Ctrl+Shift+Q, не меняет Ctrl+Q. Вставка только если исходное окно всё ещё активно; иначе перевод копируется в буфер и показывается уведомление (§5.4) |
| Установщик | Inno Setup 6.3+/7, установка **для текущего пользователя** без UAC в `%LOCALAPPDATA%\Programs\qtranslate`; задачи «меню Пуск» (вкл.), «рабочий стол» (выкл.) и «автозагрузка» (вкл.); автозагрузка синхронизируется с настройкой приложения; настройки при удалении сохраняются (§3.4) |
| Портативная сборка | Остаётся вторым артефактом релиза: zip с `bin/`, `plugins/`, `translations/`; нужна пользователям без прав установки (§3.4) |
| CI и релизы | GitHub Actions на `windows-latest`; push/PR собирают и тестируют, тег `v*` публикует установщик, zip и `SHA256SUMS.txt`; версия тега сверяется с CMake (§3.4) |
| Контрольные суммы | `SHA256SUMS.txt` остаётся отдельным ассетом релиза, но не указывается в README/Downloads; при необходимости пользователь находит файл сам |
| Linux | Отдельный этап 11 после перехода на Linux-машину: X11 — полноценный таргет, Wayland — портал/CLI-fallback с явными ограничениями; macOS не планируется (§4.14) |
| Запись в журнале Win+V при захвате | Промежуточный Ctrl+C может оставить запись; exclusion-форматы защищают только восстановление буфера. Полный обход — стратегия через UI Automation (§4.4, §9.2), принята как будущее развитие |

### 9.2. Ограничения и возможное развитие

Ограничения среды, в которой выполнялась финальная проверка (не дефекты приложения):

- Word и VS Code не установлены — захват выделения в них не проверен; Telegram авторизован, но
  личные чаты не открывались.
- Второй монитор с другим масштабом отсутствует; проверен один монитор 3840×2160 при 175%.
- Чистая Windows без Qt недоступна; проверен запуск самодостаточного `build/dist` без Qt в `PATH`.
- Реальная перезагрузка Windows для автозагрузки и внешний прокси с авторизацией не проверялись
  (локальные HTTP/SOCKS5-прокси с авторизацией покрыты `tst_networkmanager`).
- Захват в окнах администраторских процессов и запись Win+V — известные ограничения (§9.1).
- Установщик не подписан кодом: Windows SmartScreen может показать предупреждение. Сертификата
  в проекте нет; варианты и требования описаны в [`code-signing.md`](code-signing.md).
  Добавление подписи не меняет формат артефактов.
- Linux-сборка в 0.11.0 отсутствует: платформенные модули — заглушки, полноценный порт — этап 11
  (§4.14). macOS не планируется.

Возможное развитие:

1. **Захват через UI Automation** без использования буфера обмена (§4.4): цепочка «UIA →
   буфер обмена», TextPattern/GetSelection, fallback для программ без UIA.
2. **Словарные статьи** — отдельный сервис/панель, не часть перевода: LLM по JSON-схеме,
   Google gtx `dt=bd` для коротких текстов, локальный CSV-глоссарий и Wiktionary. Показывать
   части речи, значения, примеры, синонимы и транскрипцию для отдельных слов и коротких фраз.
   Дизайн — [`dictionary.md`](dictionary.md); формат Google недокументирован.
3. Дополнительные официальные сервисы (Microsoft Translator, Amazon Translate, Yandex,
   PROMT, LibreTranslate) — новый класс `TranslationService`.
4. Озвучивание текста (`QTextToSpeech`), история переводов.
5. **Linux** — этап 11: X11-реализации `platform/*`, Wayland через портал `GlobalShortcuts` и
   CLI-fallback, пакеты `.deb`/AppImage (§4.14).
6. **Локализация на основные языки мира** — этап 12 выполнен: испанский, немецкий, французский,
   португальский (Бразилия), итальянский, польский, украинский, турецкий, китайский
   (упрощённый), японский, корейский, арабский, хинди; RTL и проверка переводов в CI (§4.13).
   Переводы требуют выборочной проверки носителями перед публичным релизом.
7. **Кодовая подпись** установщика ([`code-signing.md`](code-signing.md)) и автообновление поверх
   установленной версии.
8. **Бесплатные веб-endpoints** — DeepL web уже реализован как режим `free-web` одного сервиса
   «DeepL» без ключа (§4.7.1); Yandex web, Bing web, Reverso, Mozhi, MyMemory остаются
   экспериментальными. Для всех действуют rate limit и предупреждение о неофициальности.
   Детали и приоритеты — [`free-endpoints.md`](free-endpoints.md).
9. **Локальный offline-перевод** — Argos-модели + CTranslate2 без Python: загрузка моделей по
   кнопке, pivot через английский, fallback на облако. Детали и PoC — [`local-translation.md`](local-translation.md).
10. **Проверка орфографии, грамматики и пунктуации** — отдельная вкладка, LanguageTool/Hunspell/
    Yandex Speller, замена текста исправленным. Детали — [`proofreading.md`](proofreading.md), §4.15.
11. **LLM-действия** — перевод уже работает через OpenAI-совместимый провайдер (OpenRouter по
    умолчанию, облако или локально); проверка, суммаризация, рерайтинг, объяснение и произношение
    запланированы. Словарь — отдельный `DictionaryService` ([`dictionary.md`](dictionary.md)).
    Детали — [`llm-actions.md`](llm-actions.md), §4.16.

---

## 10. Порядок реализации

Пошаговый план по этапам — в [`doc/roadmap/qtranslate/plan.md`](roadmap/qtranslate/plan.md). Подробная история старого плоского плана сохранена в `legacy-plan.md` рядом с новой картой; она будет перенесена туда на первом этапе.

---

## 11. Documentation and roadmap

### 11.1. Context

The project documentation is currently Russian and authoritative: `doc/architecture.md`, `doc/plan.md`, `doc/handoff.md`, and the design notes in `doc/`. `README.md` is English, `README.ru.md` is Russian, and the release notes in `doc/releases/` are already English. The flat `doc/plan.md` contains the detailed history of stages 1-12, the partial work on stages 13 and 16, and the remaining stages 11 and 13-17. This is a personal project with no Jira issue numbers; commit messages follow the repository's existing style without a Jira key.

The lead agent works with a tree roadmap rather than a flat plan. The existing documents do not provide that structure, so the project needs a one-time documentation migration and an English baseline.

### 11.2. Decision

Adopt a tree roadmap under `doc/roadmap/`, keep one active map for the project, and move the active documentation to English. Preserve the old flat plan as a Russian historical archive next to the new map. Do not change product code during the migration.

The active map is `doc/roadmap/qtranslate/plan.md`. The old plan will be archived as `doc/roadmap/qtranslate/legacy-plan.md` in stage 1. The map list is `doc/roadmap/README.md`. The migration is the first stage of the new map; the remaining product work follows the old plan order: Linux, free web endpoints, local offline translation, proofreading, LLM actions, and the dictionary.

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
10. Verify links, English prose, roadmap frontmatter, and that no code changed.

### 11.7. References and compatibility

- The old plan's links to `architecture.md`, `free-endpoints.md`, `local-translation.md`, `proofreading.md`, `llm-actions.md`, and `dictionary.md` become `../../...` after the move. The archive header explains the change.
- Design notes that cite "stage 13", "stage 14", and so on now cite the new roadmap stage and keep the old number in parentheses. The old plan remains the detailed history.
- Release notes are English and do not need translation. Update them only if they link to the old plan.
- Code comments and source files are not touched. If a documentation change would require a code comment change, defer it to a separate task.
- The migration is documentation-only. A build is not required. If a change accidentally touches code or tests, run the relevant Debug and Release checks and all 23 tests.

### 11.8. Verification

- Run the lead's tree overview command and confirm the root plan has valid `kind`, `status`, and `lang`.
- Check every relative Markdown link in `doc/` resolves to an existing file.
- Search for Cyrillic in active documentation, excluding `README.ru.md`, `doc/roadmap/qtranslate/legacy-plan.md`, and quoted Russian UI strings. Review headings and paragraphs manually; a raw grep can produce false positives for quoted labels.
- Search for `doc/plan.md` and `plan.md` references. Active docs should point to the roadmap; only the archive and historical notes may point to the old plan.
- Confirm `git diff --name-only` contains only documentation and `AGENTS.md`. No source, test, build, or packaging file changes.
- Ask the user to spot-check `doc/architecture.md` and `doc/handoff.md`, the two documents most likely to affect future work.
- Do not push without an explicit user request. Follow the commit rules in `AGENTS.md`.

### 11.9. Accepted decisions

| Question | Decision | Reason | Rejected alternative |
|---|---|---|---|
| Documentation language | English for active docs | User request; consistent technical docs; the lead works in English | Keep Russian; translate only the architecture; translate everything including `README.ru.md` |
| `README.ru.md` | Keep Russian | It is the user-facing Russian localisation and must stay in sync with `README.md` | Translate it; remove it |
| Code comments | Leave unchanged | Separate task; avoids unrelated code churn | Translate now |
| Old flat plan | Archive in Russian as `doc/roadmap/qtranslate/legacy-plan.md` | Preserves details and original wording; user choice | Delete it; translate it fully; keep it as the active plan |
| Roadmap location | `doc/roadmap/qtranslate/plan.md` | Project docs live in `doc/`; one map for the project | `docs/`; a separate map for the migration |
| Stage order | Migration first, then old order: Linux, web endpoints, local offline, proofreading, LLM actions, dictionary | User choice; preserves the old plan order | Linux last; migration-only map; features before migration |
| Completed stages | Brief English history in the root; details in the Russian archive | Avoids duplicating 1,488 lines; keeps history | Full translation; no history |
| Legacy manual checks | Record in `doc/handoff.md` as known gaps | Releases already shipped; they do not block new work | New acceptance stage; ignore them |
| Jira key | None | Personal project; user confirmed | Invent a key; ask the user again |
| Commit and push | Follow `AGENTS.md`; no push without explicit request | Project rules; GitHub Actions load | Push every commit; push automatically |
| Verification | Link check, English review, frontmatter check, no code changes | Documentation-only migration | Full build and test; not needed for docs |

### 11.10. Risks and mitigations

- **Translation inaccuracies.** Technical English may change a nuance. Keep the Russian archive as the original wording and ask the user to spot-check the architecture and handoff.
- **Broken links after the move.** Update every relative link in the archive and active docs, then run a link check.
- **Mixed language during migration.** The architecture document is mixed until stage 1 finishes. Keep the migration in one stage and prioritise the architecture and handoff.
- **Stale `AGENTS.md`.** It is loaded in every session. Update it in stage 1 and search for old paths.
- **Archive mistaken for the active plan.** Add a clear English header and list the archive in the map README and `AGENTS.md`.
- **Translation volume.** The active docs total roughly 4,000 lines. Split the work into tasks; start with the architecture and handoff, then the design notes, then `i18n/README.md`.
- **Linux stage may block.** The old order puts Linux first after migration. It needs a Linux machine and CI. If it is blocked, the lead asks the user whether to start independent stages earlier.
- **Release process references.** `doc/handoff.md` and `doc/code-signing.md` mention the old plan. Update them during migration.
- **Quoted Russian UI strings.** English docs may need to quote a Russian label. Allow quoted strings; do not treat them as unfinished translation.
- **Machine-only translation.** It can be inaccurate. Use human-reviewed technical English.
- **Git history and releases.** Do not push without an explicit request. Do not create a release during the migration.

### 11.11. Open items

- **Branch.** The lead asks whether to continue on `main` or create a branch. The user decides.
- **Commit and push policy.** The lead asks and follows `AGENTS.md`. No push without an explicit request.
- **Archive name and location.** Proposed as `doc/roadmap/qtranslate/legacy-plan.md`; the user can change it before stage 1 moves the file.
- **Translation glossary.** Use technical English and preserve identifiers, commands, paths, and product names. The user can spot-check and request terminology changes.
- **No Jira key.** Confirmed by the user. The lead must not ask for a Jira number.
- **Stage order.** Migration first, then the old order. Confirmed by the user. If Linux is blocked, the lead may ask to start independent stages earlier.

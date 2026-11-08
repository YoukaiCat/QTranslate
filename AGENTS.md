# AGENTS.md

## Project

- qtranslate is a tray translator for Windows and Linux: C++17, Qt 6 Widgets, CMake/Ninja, MSVC on
  Windows and GCC/Clang on Linux. X11 is the full Linux target; Wayland is limited
  (`GlobalShortcuts` portal where available, CLI commands, manual copying, warnings).
  `src/platform/*_stub.cpp` remain only for platforms without a backend.
- `qtranslate_core` (`src/core`, `src/net`, `src/services`, `src/ai`) has no Qt Widgets or WinAPI and
  is shared by the app and tests. `qtranslate` adds `src/app`, `src/ui` and `src/platform`.
- `AppController` is the only class that wires modules together. Keep network/translation logic in
  `TranslationManager`/services and UI logic in the widgets.
- Design docs are English and authoritative: `doc/architecture.md`, `doc/handoff.md`, and the design
  notes in `doc/`. The active roadmap is `doc/roadmap/qtranslate/plan.md`; the old flat plan is
  archived at `doc/roadmap/qtranslate/legacy-plan.md`, is Russian, and is historical only. Keep the
  documents in sync with behavior changes. `README.md` (English) and `README.ru.md` (Russian) must
  stay in sync.
- Current work state and release handoff: `doc/handoff.md`. Update it at the end of a session.

## Build and test

- On this machine the fast path is `cmd /c build\cli-build.cmd Debug` or `Release`. It sets up MSVC,
  Qt 6.12, CMake and Ninja, configures `build/cli-<Config>`, builds and runs CTest.
- `build/cli-build.cmd` hardcodes `C:\projects\qtranslate`, Qt 6.12 and VS 2022. Do not use it from a
  worktree or a moved checkout; use manual CMake there or update the paths explicitly.
- Manual configure:
  `cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64`
- Linux: `cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
  `cmake --build build/linux-release` and
  `xvfb-run -a ctest --test-dir build/linux-release --output-on-failure` (`xauth` is required).
  Packaging uses `packaging/linux/package.sh` on Debian 13 or in `qtranslate-trixie`;
  `--target astra` builds the native Astra artifacts on Astra Linux.
- Run one test: `ctest --test-dir build/cli-Release -R tst_settings --output-on-failure`.
  Run everything: `ctest --test-dir build/cli-Release --output-on-failure`.
- CI sets `QT_QPA_PLATFORM=offscreen`; use it for headless GUI tests. `tst_popupwindow` and
  `tst_settingsdialog` also set offscreen themselves.
- Debug and Release must build without warnings (`/W4` on Windows, `-Wall -Wextra` on Linux); all
  23 Windows tests and all 28 Linux tests must pass. The project requires Qt 6.5+; local development
  uses Qt 6.12 on Windows and Qt 6.10.2 (Astra) or Qt 6.8.2 (trixie) on Linux.
- Tests use local `QTcpServer` and fixtures; no real Google/DeepL keys or internet are required.
  `tst_hotkeymanager` is Windows-only; `tst_autostart` and `tst_secretprotector` have Windows and
  Linux variants. The X11, Wayland, selection, window-activation, and CLI tests are Linux-only.

## Git and release approval

- This is a personal project with no Jira key; commit messages follow the repository's existing
  style without a Jira number.
- Do **not** push every commit: GitHub Actions load matters. Make local commits and push only when
  the user explicitly asks, or when publishing an already approved release.
- Never create/push a release tag, publish a GitHub release or replace release assets without
  explicit user approval. Agree on the release version and scope first.
- Do not modify old releases or old tags. Fixes and improvements go into the next release/tag.
- Do not push automatically after changing READMEs, docs, installer options or code; batch changes
  locally until the user asks for a push.
- Commit as `Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>`; the repository-local
  `user.email` is set to this address. Never use a work address in commits or tags.

## Versioning and release

- Version source of truth: `project(qtranslate VERSION x.y.z)` in `CMakeLists.txt`. Mirror it in
  `resources/qtranslate.rc` (`FileVersion`/`ProductVersion`). Release tag must be `vX.Y.Z`; CI
  rejects a tag that does not match the CMake version.
- Release flow (only after explicit user approval): test Debug and Release on Windows and Linux, run
  `packaging/windows/package.ps1 -BuildDir build/cli-Release` and
  `packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux`, commit
  locally, push the release commit and new annotated tag `vX.Y.Z`, then let CI publish the installer,
  portable ZIP, `SHA256SUMS.txt`, the Linux tar.gz/`.deb`/AppImage, and `SHA256SUMS-linux.txt`.
- Do **not** list `SHA256SUMS.txt` or any checksum file in README Download sections or release
  notes Downloads. Keep `SHA256SUMS.txt` as an unlisted release asset for users who need hashes.
- Never move or rewrite an existing release tag; ship fixes as a new patch tag.
- `doc/releases/<version>.md` supplies GitHub release notes. Keep release artifacts CI-built:
  Windows local packaging uses Qt 6.12, CI uses Qt 6.8.3 LTS; Linux artifacts are built in the
  `debian:trixie` container, so their glibc baseline is trixie (2.38+), not Astra 1.8.7.
- Documentation for a release or a behavior change goes to the active roadmap
  `doc/roadmap/qtranslate/plan.md`; the archived `doc/roadmap/qtranslate/legacy-plan.md` is
  historical and is not edited.
- Code signing is not implemented. Options, SignPath Foundation requirements and integration notes
  are in `doc/code-signing.md`; do not claim releases are signed until CI verifies the signature.
- CI uses Qt 6.8.3 LTS because aqtinstall 3.3.0 cannot install Qt 6.12. Do not switch CI to 6.12
  without checking aqt support.
- CI has a Windows job and a Linux job in `debian:trixie` (root container, the shared
  `packaging/linux/trixie-packages.txt`, `timeout 300 xvfb-run -a ctest`, `package.sh` without
  `--skip-appimage`, artifact `qtranslate-linux-x64`). The `release` job needs both jobs and
  publishes both artifact sets; `SHA256SUMS-linux.txt` must stay distinct from the Windows
  `SHA256SUMS.txt`.
- `package.ps1` cleans `build/dist`, validates Qt/MSVC CRT/plugin files, then writes the ZIP,
  installer and `SHA256SUMS.txt`. Inno Setup 6.3+/7 is required.

## Installer gotchas

- `packaging/windows/qtranslate.iss` contains Russian custom messages and must stay UTF-8 **with
  BOM**. Preserve the BOM when editing.
- Tasks: Start menu is checked by default, desktop is unchecked, autostart is checked. Autostart must
  stay synchronized with `[app] autostart` in `%APPDATA%\qtranslate\qtranslate.ini` and the
  `HKCU\...\Run` value; otherwise the first app launch removes the Run entry.
- Inno's `GetIniBool` did not parse `false` correctly here. `StoredAutostart` uses `GetIniString`
  and compares; do not replace it with `GetIniBool`.
- Task names are not present in `TasksList.Items`. The current code finds the autostart checkbox by
  its caption (`FindAutostartItemIndex`). Test both interactive and silent installs after changing
  task logic.
- Installer tests write real `%APPDATA%`, `HKCU\...\Run` and the Start menu. Back up/restore them,
  stop qtranslate first, and remember that a running exe locks DLLs and blocks uninstall via
  `AppMutex`.
- `main.cpp` holds the `Local\qtranslate-app` mutex for the installer's `AppMutex`. Keep it in sync
  with the installer if the mutex changes.

## Runtime and network

- Google's free endpoint must stay HTTP/1.1 (`Http2AllowedAttribute = false`); HTTP/2 caused
  systematic `429` responses. Do not remove this.
- Settings default to `%APPDATA%\qtranslate\qtranslate.ini` on Windows and
  `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` on Linux; a `qtranslate.ini` next to the exe switches
  to portable mode. API keys and proxy passwords use DPAPI on Windows, and libsecret or a `0600`
  fallback file with a warning on Linux.
- Logs must never contain translated text, API keys or passwords. `--verbose` writes to
  `%LOCALAPPDATA%\qtranslate\qtranslate.log` on Windows and
  `$XDG_STATE_HOME/qtranslate/qtranslate.log` on Linux.
- `AppController::start()` calls `Autostart::sync(settings.general.autostart)`; keep installer-written
  settings consistent with it.
- Deployment intentionally excludes `vc_redist.x64.exe`, D3D/DXC compilers and `opengl32sw.dll`
  (Qt Widgets uses the raster engine), and deploys Qt's own translations for the supported UI
  languages (`ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar`). Qt 6.12 has no Hindi translation, so
  Hindi standard dialogs stay English. Application translations are embedded in the exe. CMake
  installs the MSVC CRT explicitly.
- UI strings use `tr()`; Russian is in `i18n/qtranslate_ru_RU.ts` and is compiled by
  `qt_add_translations`. Run `lupdate`/`lrelease` when adding strings and keep both READMEs and
  screenshots current.

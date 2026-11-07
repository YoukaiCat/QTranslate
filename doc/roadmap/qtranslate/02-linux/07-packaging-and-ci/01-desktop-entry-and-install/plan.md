---
kind: task
status: done
detailed: true
---

# Desktop entry, hicolor icon, and the Linux install layout

## Task

Add the Linux desktop integration files and make `cmake --install` produce the one layout that the
`.deb` and the portable archives reuse: `share/applications/qtranslate.desktop` and
`share/icons/hicolor/256x256/apps/qtranslate.png` from `resources/icons/app.png`. The desktop entry
must pass `desktop-file-validate`, and the Windows deploy script must stop running on Linux: the
generic Qt deploy tool copies no Qt libraries, writes a trixie-specific `bin/qt.conf`, and logs a
`DEPLOY_TOOL_OPTIONS` warning, so the packaging script owns the Linux bundle instead.

Facts established before this task: `resources/icons/app.png` is a 256x256 RGBA PNG;
`qt_standard_project_setup()` sets the Linux install RPATH to `$ORIGIN:$ORIGIN/../lib`
(`Qt6CoreMacros.cmake:3448` in trixie's Qt 6.8.2 and the 6.10.2 host file, and the installed binary
shows it); `cmake --install` on Linux currently writes `bin/qt.conf` with
`Plugins = lib/x86_64-linux-gnu/qt6/plugins` and copies Qt translations to
`share/qt6/translations`, but no Qt libraries; under Xvfb the application window reports
`WM_CLASS(STRING) = "qtranslate", "QTranslate"` when started with `[app] startMinimized=false`.

## Steps

1. Create `packaging/linux/qtranslate.desktop` (UTF-8, LF, trailing newline) with this content:

   ```ini
   [Desktop Entry]
   Type=Application
   Name=QTranslate
   GenericName=Translator
   Comment=Translate the selected text in a popup, in the main window, or in place
   Exec=qtranslate
   Icon=qtranslate
   Terminal=false
   Categories=Utility;TextTools;
   Keywords=translate;translation;translator;language;dictionary;
   StartupNotify=false
   StartupWMClass=QTranslate
   ```

   - `Exec=qtranslate` is deliberate: the `.deb` installs the binary to `/usr/bin`, and the
     portable archive is started from its own `bin/`. Do not add `%U`/`%F`: the application does
     not open files, and task 06 ignores positional arguments only as a safety net.
   - `Icon=qtranslate` must match the installed icon file name; `Categories` and `Keywords` must be
     registered values with trailing semicolons. `StartupWMClass=QTranslate` matches the observed
     window class, while the instance name `qtranslate` matches the desktop file name.
   - Do not add `MimeType` or a `Version` key. English only, like the other repository files.
   - Run `desktop-file-validate packaging/linux/qtranslate.desktop` and expect exit code 0; the
     candidate above was validated with `desktop-file-utils` 0.27 on Astra and `desktop-file-utils`
     is the same check tool in trixie.

2. Install the desktop file and the icon from CMake.
   - In `CMakeLists.txt`, after the existing `install(TARGETS qtranslate RUNTIME DESTINATION
     ${CMAKE_INSTALL_BINDIR})` block (`CMakeLists.txt:169-171`), add a Linux-only block:

     ```cmake
     if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
         install(FILES packaging/linux/qtranslate.desktop
             DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
         install(FILES resources/icons/app.png
             DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
             RENAME qtranslate.png)
     endif()
     ```

   - `${CMAKE_INSTALL_DATADIR}` is `share` under a `/usr` or `/usr/local` prefix; it is provided by
     `GNUInstallDirs`, which `qt_standard_project_setup()` includes. Do not hardcode `share`.
   - The icon is installed at exactly 256x256 by copying `resources/icons/app.png`; do not resize
     or upscale, and do not add an image tool. Smaller hicolor sizes are optional and not needed.
   - `install(FILES ... RENAME ...)` keeps the source file reusable; `Icon=qtranslate` and the
     installed `qtranslate.png` must stay in sync.

3. Guard the Windows deploy block so Linux installs only the files this stage owns.
   - Wrap `qt_generate_deploy_app_script(...)` and `install(SCRIPT ${deploy_script})`
     (`CMakeLists.txt:173-192`) in `if(WIN32) ... endif()`. Keep the options, comments, and the
     Windows behavior byte-identical; the MSVC CRT block (`CMakeLists.txt:198-215`) already has its
     own `if(MSVC)` and is untouched.
   - Reason to record in the task outcome: on Linux the deploy script is the generic deploy tool,
     which logs `DEPLOY_TOOL_OPTIONS has no effect`, deploys no Qt libraries (their system
     directories are excluded), and writes a `qt.conf` that points at the trixie Qt plugin path.
     That `qt.conf` is wrong for every portable archive; the packaging script in the next child
     writes its own. Removing the block also removes the `share/qt6/translations` copy; the
     packaging script copies the Qt translations it needs explicitly.
   - After the change, `cmake --install <build> --prefix <tmp>` must produce only `bin/qtranslate`,
     `share/applications/qtranslate.desktop`, and
     `share/icons/hicolor/256x256/apps/qtranslate.png` (plus nothing else from the application
     install rules), and the installed binary keeps `RUNPATH $ORIGIN:$ORIGIN/../lib`.

4. Align the desktop file name and the Wayland app id.
   - In `src/main.cpp`, after `QApplication::setApplicationVersion(AppInfo::version());`
     (`src/main.cpp:37`), add
     `QGuiApplication::setDesktopFileName(QStringLiteral("qtranslate"));` with a short comment that
     this must match `share/applications/qtranslate.desktop`. Add `#include <QGuiApplication>` if
     the compiler does not see the declaration through `QApplication`.
   - This makes the Wayland app id `qtranslate`; on X11 the observed WM_CLASS stays
     `"qtranslate", "QTranslate"`, and `StartupWMClass` covers launchers that match the class. It is
     a no-op on Windows and changes no test.
   - Do not set `StartupWMClass=qtranslate` (the instance name): the observed class is `QTranslate`;
     `desktop-file-validate` accepts either, but the class is what window managers use.

5. Add the validation tool to the packaging image.
   - `packaging/linux/trixie.Dockerfile`: add `desktop-file-utils` to the `apt-get install` list
     (`packaging/linux/trixie.Dockerfile:15-34`). It provides `desktop-file-validate`; the image
     currently has no such tool.
   - Update the stale header comment `# Task 06 adds the packaging tooling (dpkg-dev,
     desktop-file-utils, AppImage).` so it names this stage and does not claim that a single task
     adds everything; later children add `dpkg-dev`, `patchelf`, `file`, `curl`, and the downloaded
     AppImage tools.

6. Verify on trixie and Astra; review Windows by diff.
   - `debian:trixie` (Qt 6.8.2, GCC 14.2): build the image from
     `packaging/linux/trixie.Dockerfile`, configure Debug and Release from a read-only source mount
     into `/tmp` with `-DCMAKE_PREFIX_PATH=/usr`, build with `-Wall -Wextra`, grep the logs for
     `warning:` and expect none. Run
     `timeout 300 xvfb-run -a ctest --test-dir <build> --output-on-failure` and expect 28/28.
     `cmake --install <release-build> --prefix /tmp/qtranslate-install`; verify the three installed
     paths, `desktop-file-validate /tmp/qtranslate-install/share/applications/qtranslate.desktop`,
     `test ! -e /tmp/qtranslate-install/bin/qt.conf`, no `share/qt6`, and
     `readelf -d /tmp/qtranslate-install/bin/qtranslate | grep RUNPATH` contains
     `$ORIGIN:$ORIGIN/../lib`. Confirm the install log has no `DEPLOY_TOOL_OPTIONS` warning.
   - Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2): native Debug and Release in
     `build/linux-debug`/`build/linux-release`, 0 warnings, CTest 28/28. After every rebuild run
     `sudo /usr/bin/fly-set-execid.sh` so the X11 tests keep the trusted-client attribute and
     record the unchanged counts (16/16, 20/20, 12/12, 13/13, 7/7, 6/6, secret 7+1 or 8/8,
     `tst_cli` 20/20). Run `cmake --install` into a temporary prefix and `desktop-file-validate` on
     the installed file. Start the Release binary under Xvfb with a temporary `HOME` and
     `[app] startMinimized=false` and confirm the window's `WM_CLASS` is
     `"qtranslate", "QTranslate"`; record the `StartupWMClass` match.
   - Windows: do not build (no MSVC). Review `git diff` that the deploy block's contents, the MSVC
     CRT block, the Windows install target, and `if(WIN32)` branches are unchanged; the only shared
     code change is the `setDesktopFileName` line. Record the Windows 23 tests as not performed.
   - `git diff --check`; confirm only this task's CMake, source, desktop file, and Dockerfile
     changed; do not edit `README.md`, `README.ru.md`, `doc/architecture.md`, `AGENTS.md`,
     `doc/handoff.md`, the stage plan, or the journal.

## Done when

- `packaging/linux/qtranslate.desktop` exists with `Type=Application`, `Name=QTranslate`,
  `Exec=qtranslate`, `Icon=qtranslate`, `Terminal=false`, `Categories=Utility;TextTools;`,
  `Keywords=...`, and passes `desktop-file-validate` with exit code 0 in the source tree and on the
  installed copy.
- `cmake --install` installs `<prefix>/share/applications/qtranslate.desktop` and
  `<prefix>/share/icons/hicolor/256x256/apps/qtranslate.png`; the PNG is the 256x256
  `resources/icons/app.png` renamed to match `Icon=qtranslate`.
- On Linux the install tree has no `bin/qt.conf` and no `share/qt6/translations` from the deploy
  script, the install log has no `DEPLOY_TOOL_OPTIONS` warning, and the installed binary keeps
  `RUNPATH $ORIGIN:$ORIGIN/../lib`. The Windows deploy block, MSVC CRT block, and Windows install
  rules are unchanged by diff.
- `QGuiApplication::setDesktopFileName("qtranslate")` makes the Wayland app id match the desktop
  file name; the observed X11 `WM_CLASS` is `qtranslate`/`QTranslate` and matches
  `StartupWMClass=QTranslate`.
- `packaging/linux/trixie.Dockerfile` installs `desktop-file-utils`, and its comment no longer
  claims that one old task adds all packaging tools.
- Debian 13 (trixie) and Astra Linux 1.8.7 build Debug and Release with no `-Wall -Wextra`
  warnings and CTest 28/28; the Astra rebuild is followed by `sudo /usr/bin/fly-set-execid.sh`.
  Windows Debug/Release and the 23 tests are recorded as not performed (no MSVC). The READMEs,
  architecture, `AGENTS.md`, the handoff, the stage plan, and the journal are untouched; the
  desktop file content, install paths, and the observed `WM_CLASS` are reported to the lead.

## Risks

- `desktop-file-validate` is strict about registered categories and trailing semicolons. Use
  `Utility;TextTools;` and `Keywords=...;` exactly; a missing final semicolon or `TextTools` without
  `Utility` fails the gate. The candidate content above was validated locally with 0.27, but run
  the check in the trixie image as the authoritative gate.
- `StartupWMClass` must match the real window class. The observed X11 class is `QTranslate`
  (instance `qtranslate`); do not guess a lowercase class. On Wayland the app id comes from
  `setDesktopFileName`, not from `StartupWMClass`.
- Guarding the deploy block must not change the Windows options or comments. The generic Linux
  deploy tool is not a fallback packager: it copies no Qt libraries and writes a wrong `qt.conf`.
  Removing the block is the fix; do not add a second Linux `install(SCRIPT ...)`.
- `CMAKE_INSTALL_DATADIR` depends on `GNUInstallDirs`; `qt_standard_project_setup()` includes it.
  If a future CMake configuration removes that call, fall back to `share` explicitly rather than
  writing an empty destination.
- The icon is exactly 256x256. Do not upscale it or add ImageMagick; smaller hicolor sizes are
  optional and would add a build-time tool.
- `setDesktopFileName` is a one-line shared-code change. It is a no-op on Windows, but it must not
  change the existing Windows behavior or tests; review the diff and record Windows as not
  performed. If the reviewer rejects the line, the desktop file still installs, but the Wayland
  app id stays `QTranslate` and may not associate with `qtranslate.desktop`; do not drop the line
  silently.
- Linux stays at 28 tests; no test is added or removed. The `.desktop` and icon are checked by the
  commands in step 6, not by CTest.
- The `.deb` and tar.gz later stage from this install prefix. Do not add a second copy of the
  desktop file or icon to a packaging script; one CMake layout must feed both.

## Outcome

Done. The Linux install layout now carries the desktop entry and the 256x256 icon, and Linux no
longer runs the Windows deploy script. Linux stays at 28 tests; Windows keeps 23.

- Files: new `packaging/linux/qtranslate.desktop`; modified `CMakeLists.txt`, `src/main.cpp` and
  `packaging/linux/trixie.Dockerfile`. The READMEs, architecture, `AGENTS.md`, the handoff, the map
  files, `i18n/`, `tests/` and `packaging/windows/` are unchanged.
- Desktop entry: `Type=Application`, `Name=QTranslate`, `GenericName=Translator`,
  `Comment=Translate the selected text in a popup, in the main window, or in place`,
  `Exec=qtranslate`, `Icon=qtranslate`, `Terminal=false`, `Categories=Utility;TextTools;`,
  `Keywords=translate;translation;translator;language;dictionary;`, `StartupNotify=false`,
  `StartupWMClass=QTranslate`. No `%U`/`%F`, `MimeType` or `Version`. The file is UTF-8 (ASCII
  bytes), LF, with a trailing newline. The text is desktop metadata, not `tr()` UI strings, so no
  `.ts` or i18n change was needed.
- CMake: the Linux-only block after `install(TARGETS qtranslate ...)` installs
  `${CMAKE_INSTALL_DATADIR}/applications/qtranslate.desktop` and
  `${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps/qtranslate.png` (`install(FILES ... RENAME)`
  copies `resources/icons/app.png` unchanged, not resized). `cmake --install` on trixie and Astra
  produces exactly those three files plus `bin/qtranslate`; the installed PNG is byte-identical to
  the source and `desktop-file-validate` accepts both the source and the installed copy
  (0.28 in trixie, 0.27 on Astra). `${CMAKE_INSTALL_DATADIR}` resolves to `share` under `/usr` and
  `/usr/local`; it is never hardcoded.
- Deploy guard: `qt_generate_deploy_app_script(...)` and `install(SCRIPT ${deploy_script})` are
  wrapped in `if(WIN32)` without reindenting or changing the options, comments or Windows behavior
  (byte-identical to the previous block; the MSVC CRT block keeps its own `if(MSVC)`). Reason:
  on Linux that script is the generic deploy tool, which logs `DEPLOY_TOOL_OPTIONS has no effect`,
  copies no Qt libraries, writes a trixie-specific `bin/qt.conf`, and would copy
  `share/qt6/translations`. The packaging script in child 02 owns the Linux bundle and writes its
  own `qt.conf`; the install log now has no `DEPLOY_TOOL_OPTIONS` warning.
- Installed binary: `RUNPATH` stays `$ORIGIN:$ORIGIN/../lib` on both systems; the install tree has
  no `bin/qt.conf` and no `share/qt6`.
- App identity: `QGuiApplication::setDesktopFileName(QStringLiteral("qtranslate"))` after
  `setApplicationVersion` makes the Wayland app id `qtranslate`, matching the desktop file name; no
  extra include was needed because `QApplication` already includes `QGuiApplication`. Under Xvfb the
  Release binary with a temporary `HOME` and `[app] startMinimized=false` reports
  `WM_CLASS(STRING) = "qtranslate", "QTranslate"`, so the observed class still matches
  `StartupWMClass=QTranslate` and `setDesktopFileName` changes no X11 behavior. The line is a no-op
  on Windows; the only shared-code change in the diff.
- Dockerfile: `desktop-file-utils` is installed (provides `desktop-file-validate`), and the header
  comment now names stage `07-packaging-and-ci` and lists what later children add (`dpkg-dev`,
  `patchelf`, `file`, `curl`, the AppImage tools) instead of attributing everything to one old task.
- Verification: `debian:trixie` (Qt 6.8.2, GCC 14.2) Debug and Release rebuild with no
  `-Wall -Wextra` warnings, CTest 28/28 in both (`tst_hotkeymanager_x11` 16/16,
  `tst_hotkeymanager_wayland` 20/20, `tst_selectiongrabber_linux` 12/12,
  `tst_windowactivation_linux` 13/13, `tst_hotkey_selection_x11` 7/7, `tst_autostart` 6/6,
  `tst_secretprotector` 7 passed + 1 skipped without a session keyring, `tst_cli` 20/20). Astra
  Linux 1.8.7 (Qt 6.10.2, GCC 12.2) Debug and Release come out clean and 28/28 after
  `sudo /usr/bin/fly-set-execid.sh` (secret 8/8; the other counters unchanged). Windows
  Debug/Release and the 23 tests were not performed (no MSVC); the diff review confirms the deploy
  block, MSVC CRT block, Windows install target and `if(WIN32)` branches are unchanged apart from
  the new wrapper.
- Not performed: a real Wayland compositor was not available, so the app-id-to-desktop-file
  association is verified only by the code and by the unchanged X11 `WM_CLASS`; no test was added
  for the desktop file (the plan checks it with `desktop-file-validate` and the install tree).
- Later children: the `.deb`/tar.gz must stage from this one CMake install layout and must not add
  a second copy of the desktop file or icon. `Icon=qtranslate` and the installed
  `qtranslate.png` stay in sync; smaller hicolor sizes are optional and were not added.
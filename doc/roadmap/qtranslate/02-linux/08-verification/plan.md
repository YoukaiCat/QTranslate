---
kind: task
status: done
detailed: true
depends: [01-linux-build-baseline, 02-global-hotkeys, 03-x11-selection-and-inplace, 04-x11-hotkey-grab-release, 05-xdg-autostart-and-secrets, 06-cli-and-desktop-integration, 07-packaging-and-ci]
---

# Linux verification

## Task

Final gate for stage 2. Verify every `## Done when` of the stage with evidence: the trixie container
build and tests, the native Astra build, the X11 scenarios, the limited Wayland behaviour,
autostart, secrets, the trixie artifacts and the native Astra artifacts, the CI workflow, and the
documentation (the lead owns the docs; this task checks and reports gaps, it does not edit them).
Record Windows, a real portal-capable Wayland desktop, and an actual GitHub tag publication as not
performed when they are unavailable, never as passed.

The primary distro is Debian 13 (trixie, Qt 6.8.2); the machine is Astra Linux with X11 and Qt
6.10.2. Tests run under `xvfb-run` in the trixie container. Measured in the packaging plan: the
trixie `.deb` cannot install on Astra (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`) and the trixie
`tar.gz`/AppImage cannot run there (bundled Qt 6.8.2 needs `GLIBC_2.38`; Astra has 2.36), so the
native Astra build is the Astra path and must not fall out. Wayland is limited:
`GlobalShortcuts` portal where available, manual copying, CLI commands, and explicit warnings; no
`ydotool`/`wtype` or other synthetic input.

This task changes no code, tests, CMake, packaging file, or documentation. Its deliverable is the
evidence recorded in `## Outcome`: command output, test and artifact facts, screenshot and log
paths, the stage-criteria traceability table, and a gap list for the lead. If a check fails, stop
and report the defect with a reproducer; the coordinator and programmer fix code, the lead fixes
documentation, and the gate is re-run. Never edit a source or documentation file to make a check
pass, and never report a check that did not run as passed.

The steps run the real-session checks first: the packaging acceptance did not repeat the real `:0`
GUI, and a defect found there would invalidate later build results only if code changed, which this
task must not do.

## Steps

1. Establish the baseline and the evidence workspace.
   - Record `git rev-parse HEAD` (expected `0a3a762` or later), `git status --short` (only this map
     file may be modified), `git log --oneline -8`, `git log --oneline origin/main..HEAD` (local
     commits not pushed), and `git tag --list 'v0.12*'` (must be empty; 0.12.0 is a later user
     decision).
   - Record the host facts: `/etc/os-release` (`ID=astra`, `VERSION_ID=1.8_x86-64`; the file does
     not contain `1.8.7`), `qtpaths6 --query QT_VERSION` (6.10.2), `gcc --version` (12.2),
     `cmake --version` (3.31.6), `ldd --version` (glibc 2.36), `docker images` (`qtranslate-trixie:latest`
     `b17f22fb75d9`, `debian:trixie` `51e269e6af18`), `weston --version` (10.0.1), and that
     `xvfb-run`, `xdotool`, `xclip`, `xsel`, `scrot`, `import`, `strace`, `getfattr`, `gtk-launch`,
     `kate`, `patchelf`, `rsync`, and Ruby Psych are present. Record `sudo -n true` (passwordless).
   - Confirm the start state: `pgrep -a qtranslate` is empty, `dpkg-query -W -f='${Status}' qtranslate`
     reports not installed, and no `qtranslate` package is left installed.
   - Create `/tmp/opencode/verify-linux/` for logs, screenshots, and extracted trees. Every command
     writes its output there; the Outcome cites these files. Do not use the repository's `build/`
     for new root-owned output.
   - Copy the stage `## Done when` bullets into an empty traceability table at the top of the
     evidence notes; each row gets the step and the exact evidence (test log line, hash, screenshot,
     command output).
   - Do not build or install anything yet.

2. Rebuild the Astra Release tree at HEAD and smoke-start it on the real X11 session.
   - Configure and build incrementally (the tree is a Release build with `CMAKE_PREFIX_PATH=/usr`,
     Qt 6.10.2):
     `cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr`
     and `cmake --build build/linux-release --parallel`, both with `2>&1 | tee` into
     `/tmp/opencode/verify-linux/astra-release-*.log`; fail on any `warning:` line.
   - Re-run `sudo /usr/bin/fly-set-execid.sh` (the Parsec `security.exec_id` attribute is lost on
     rebuild) and record `getfattr -n security.exec_id` for `build/linux-release/tests/tst_hotkeymanager_x11`
     and `build/linux-release/tests/tst_hotkey_selection_x11` (expected `0sEA==`, role `KBD_R`).
     Without this attribute the two X11 tests `QSKIP`; a skip is not a pass.
   - Smoke start with a temporary `HOME`/`XDG_CONFIG_HOME`/`XDG_STATE_HOME`/`XDG_RUNTIME_DIR`
     (mode 0700 for the runtime) and `DISPLAY=:0`: `build/linux-release/qtranslate --verbose`,
     poll `xdotool search --name QTranslate` (about 10 s), record the window title and geometry from
     `xwininfo -root -tree`, then `kill -TERM` and confirm no process and no window remain. This is
     the "native Astra build launches under the real X11 session" evidence and the binary used by the
     matrix.
   - `build/linux-release/qtranslate --version` under `xvfb-run -a` prints `QTranslate 0.11.0`.

3. X11 scenario matrix on the real `:0` session.
   - Preconditions and safety: `pgrep -a qtranslate` empty; a fresh temporary
     `HOME`/`XDG_CONFIG_HOME`/`XDG_STATE_HOME`/`XDG_RUNTIME_DIR`; `DISPLAY=:0`; `kate` as the target
     application; the trusted `build/linux-release/qtranslate` (listed in
     `/etc/X11/trusted.d/qtranslate`, so `XGrabKey` is allowed). Write
     `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` before the first start with
     `[app] restoreClipboard=true` and `[translation] service=google`, `sourceLang=en`,
     `primaryLang=de`, so `Hello, world` → `Hallo, Welt`. If Google answers `429`, repeat once with
     `service=deepl` plus `[deepl] mode=free-web` and record which provider was used. Never touch
     the user's real config, do not leave windows, and close the application at the end.
   - Start the app hidden: `build/linux-release/qtranslate --minimized --verbose` with the temporary
     environment. The verbose log is `$XDG_STATE_HOME/qtranslate/qtranslate.log`.
   - Ctrl+Q: seed the clipboard with `printf 'QTV-clip-sentinel' | xclip -selection clipboard`; open
     `kate` on a file containing `Hello, world` and a second line, select the text (`ctrl+a`), press
     `xdotool key ctrl+q`, wait up to 15 s. Evidence: `xdotool search --name QTranslate`,
     `xwininfo -root -tree | grep -i qtranslate`, `import -window <popup-id> /tmp/opencode/verify-linux/x11-ctrlq.png`
     (capture the QTranslate window only; a full-screen `scrot` may capture the user's desktop), the
     log lines `Hotkey activated: 0` and `Popup shown ... after Ctrl+Q`, and
     `xclip -selection clipboard -o` equals the sentinel (clipboard restored). Read the screenshot to
     confirm the popup shows the German translation; the log alone is not proof of the visible
     result.
   - Ctrl+Shift+Q: with the text selected in `kate`, `xdotool key ctrl+shift+q`, wait, then
     `xdotool key ctrl+s` and read the file back: the selection is replaced by the translation, the
     clipboard sentinel is restored, and the log has `Hotkey activated: 1`.
   - Ctrl+W: with the text selected in `kate`, `xdotool key ctrl+w`; the main window appears with the
     translation (screenshot; optionally `xdotool key ctrl+a ctrl+c` and `xclip -selection clipboard -o`
     contains `Hallo, Welt`), the log has `Hotkey activated: 2`; close the window with `alt+F4` and
     confirm the process stays in the tray. The passive grab consumes the combination; if `kate`
     itself reacts (for example Ctrl+W closes the document), the grab was not active — record it as a
     failure, not as a pass.
   - Tray menu: screenshot the panel, locate the tray icon, click it with `xdotool mousemove X Y
     click 1`, screenshot the menu. Check **Show window**, **Pause hotkeys**, **Service**,
     **Settings**, **Quit**. Pause → `ctrl+q` produces no popup and the log shows the suspension;
     resume and confirm the hotkey works again. Show window opens the main window; Settings opens
     the dialog; Quit ends the process. If the session has no StatusNotifier host, the application
     shows one `System tray unavailable` warning: record the exact reason and mark the tray-menu item
     as not performed, never as passed.
   - Settings open and save: in the dialog change a harmless setting (for example clear and set
     **Restore clipboard** again, or switch `uiLanguage` to English and back), accept, and confirm
     the temporary INI changed; restart once and confirm the value persisted; restore it.
   - Autostart: in Settings → General enable autostart, confirm
     `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` exists with `Exec="<path>" --minimized`,
     `Icon=qtranslate`, `Type=Application`, and `[app] autostart=true` in the INI; disable it and
     confirm the file is removed. Record the file content and the checkbox screenshot.
   - CLI without a running instance: quit the app; for `--translate-popup`, `--translate-main`, and
     `--translate-inplace` start the app with the option, wait, and record the popup/main-window/
     in-place result, the exit code 0, and that the process stays in the tray (`pgrep -c qtranslate`
     is 1). With an instance running (`--minimized`), send each command again: the secondary exits 0,
     the primary log has one `Received command translate-popup|translate-main|translate-inplace`
     line, and only one process remains. `--minimized --translate-popup` stays in the tray;
     `--translate-popup --translate-main` and `--bogus` exit 1 with a clear message; `--help` lists
     the three options.
   - Single-instance activation: run `qtranslate` with no arguments while the instance runs; the main
     window is shown, the second process exits 0, and the log has `Received command activate`.
   - Record the screenshots and the log excerpt per scenario under `/tmp/opencode/verify-linux/`.
     If `:0` is busy or unavailable, run the same sequence under `xvfb-run` with `xdotool`, record
     the real-session item as not performed with the reason, and never claim a real-session pass.

4. Wayland: the limited path.
   - Weston headless: a private `XDG_RUNTIME_DIR` (0700); `weston --backend=headless-backend.so
     --socket=wayland-verify --width=1280 --height=800` in the background; wait for
     `$XDG_RUNTIME_DIR/wayland-verify`. Start the app with `DISPLAY` unset,
     `WAYLAND_DISPLAY=wayland-verify`, `QT_QPA_PLATFORM=wayland`,
     `DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent`, temporary XDG directories, `--verbose`, under
     `strace -f -e connect -o .../wayland-connect.log`. Evidence: the app starts and stays alive,
     the log contains `GlobalShortcuts portal unavailable:` with the exact reason and the
     portal/CLI fallback sentence, `wayland-connect.log` has no `/tmp/.X11-unix` connection, and
     `grep -rn 'ydotool\|wtype' src/` is empty (no synthetic input in the tree).
   - Visible warning: headless Weston has no visible output, so run a second pass with
     `weston --backend=x11-backend.so` on `:0` (the module is present) and capture the application's
     `Limited Wayland support` dialog from the Weston X11 window with `import -window`/`scrot`
     (`/tmp/opencode/verify-linux/wayland-warning.png`). If the nested compositor cannot be started,
     record the exact reason and keep the log line as the evidence, marked as not visually confirmed.
   - Clipboard and in-place on Wayland: `wl-copy`/`wl-paste` are not installed on the host. If the
     check needs them, install `wl-clipboard` with `sudo apt-get install -y wl-clipboard` (record the
     version; remove it afterwards if the user does not want it) and use them against the Weston
     socket: seed `Hello, world`, run `--translate-popup`, confirm the popup translates the copied
     text, run `--translate-inplace` and confirm `wl-paste` returns `Hallo, Welt` and the in-place
     message is shown; focus the application surface inside Weston first, because a Wayland clipboard
     offer is delivered to the focused client. If the headless seat cannot deliver an offer (no
     keyboard focus), record the exact failure and mark the live clipboard/in-place check as not
     performed; the automated coverage is `tst_selectiongrabber_linux` (Wayland reads the clipboard,
     no XTest) and `tst_windowactivation_linux` (no paste on Wayland).
   - Portal happy path: `tst_hotkeymanager_wayland` 20/20 in steps 5 and 6 is the automated gate; a
     real KDE/GNOME `GlobalShortcuts` desktop is not performed (Astra runs X11; Weston has no
     portal), with the reason. Do not claim it.
   - Confirm the in-place path never pastes and never synthesizes input on Wayland; the message says
     the translation was copied to the clipboard.

5. Astra Linux: the native Debug and Release test gate at HEAD.
   - Configure and build `build/linux-debug` and `build/linux-release` incrementally (both are
     `CMAKE_PREFIX_PATH=/usr` trees) with `-Wall -Wextra`; logs under
     `/tmp/opencode/verify-linux/astra-{debug,release}-*.log`; fail on any `warning:` line.
   - Run `sudo /usr/bin/fly-set-execid.sh` after the builds, then
     `timeout 300 xvfb-run -a ctest --test-dir build/linux-<cfg> --output-on-failure` for both;
     expect `100% tests passed, 0 tests failed out of 28` (the gate is `xvfb-run`; a bare `ctest` on
     the live `:0` fails two focus tests by design and is not the gate).
   - Record the per-binary counts: `tst_hotkeymanager_x11` 16/16, `tst_hotkeymanager_wayland` 20/20,
     `tst_selectiongrabber_linux` 12/12, `tst_windowactivation_linux` 13/13,
     `tst_hotkey_selection_x11` 7/7, `tst_autostart` 6/6, `tst_secretprotector` 8/8 (the real libsecret
     round-trip; the session keyring is available), `tst_cli` 20/20; the 20 shared tests pass.
     Confirm no `QSKIP` in the two X11 tests; a skip means the trusted-client attribute was lost.
   - No warnings, no failed tests, and no skipped X11 test may be reported as a pass if it did not
     run. Windows is not performed here (no MSVC).

6. Debian 13 (trixie): the Debug and Release test gate at HEAD.
   - Work on a fresh copy so root-owned files never appear in the repository:
     `rm -rf /tmp/opencode/verify-linux/trixie-src &&
     rsync -a --exclude .git --exclude build ./ /tmp/opencode/verify-linux/trixie-src/` and mount it
     at `/src` in the prebuilt image (same 21-package environment as the CI job):
     ```
     docker run --rm -v /tmp/opencode/verify-linux/trixie-src:/src -w /src -e HOME=/tmp \
       qtranslate-trixie:latest bash -lc '
         set -euo pipefail
         mkdir -p build/verify-logs
         for cfg in Debug Release; do
           cmake -S . -B build/$cfg -G Ninja -DCMAKE_BUILD_TYPE=$cfg -DCMAKE_PREFIX_PATH=/usr 2>&1 | tee build/verify-logs/cmake-$cfg.log
           cmake --build build/$cfg --parallel 2>&1 | tee build/verify-logs/build-$cfg.log
           if grep -Eq "warning:" build/verify-logs/build-$cfg.log; then echo "warnings in $cfg"; exit 1; fi
           timeout 300 xvfb-run -a ctest --test-dir build/$cfg --output-on-failure 2>&1 | tee build/verify-logs/ctest-$cfg.log
           xvfb-run -a build/$cfg/qtranslate --version | tee build/verify-logs/version-$cfg.log
         done'
     ```
     The logs stay in the mounted copy (`/tmp/opencode/verify-linux/trixie-src/build/verify-logs/`),
     not in the container's `/tmp`, so the Outcome can cite them.
     Record the image ID `b17f22fb75d9`, `qtpaths6 --query QT_VERSION` (6.8.2), `gcc --version`
     (14.2), and the exact `--version` output (`QTranslate 0.11.0`).
   - Record the CTest summary (28/28) and the per-binary counts as in step 5. In this image
     `tst_secretprotector` is 7 passed + 1 skipped because there is no session keyring: record the
     skipped libsecret round-trip as not performed here, covered by the Astra run in step 5.
   - `tst_hotkeymanager_x11` and `tst_hotkey_selection_x11` must pass here without the Parsec
     attribute (trixie has no XPARSEC); record 16/16 and 7/7.

7. Astra Linux: the native artifacts and the trixie-artifact failures.
   - Verify the accepted Astra artifacts by hash before using them:
     `sha256sum build/artifacts-linux/qtranslate-0.11.0-linux-x86_64-astra.tar.gz`
     (`f8e5d7e4…77e80d`) and `.../qtranslate_0.11.0+astra1_amd64.deb` (`6c2f24bb…9f797f`);
     `(cd build/artifacts-linux && sha256sum -c SHA256SUMS-linux-astra.txt)` passes.
   - Astra `tar.gz` at runtime: extract to `/tmp/opencode/verify-linux/astra-tar` with
     `LD_LIBRARY_PATH` unset and temporary `HOME`/XDG directories; `xvfb-run -a bin/qtranslate
     --version` prints `QTranslate 0.11.0`; `ldd` on the binary, every `lib/` file, and every plugin
     has no `not found` and resolves Qt 6.10.2/ICU 72 inside the extract; `patchelf --print-rpath`
     is `$ORIGIN` for libraries and `$ORIGIN/../../lib` for plugins; `QT_DEBUG_PLUGINS=1` loads the
     bundled offscreen plugin; `desktop-file-validate` passes on the bundled desktop file.
   - Astra `.deb`: `dpkg-deb --field Version,Depends,Installed-Size` (`0.11.0+astra1`,
     `libglib2.0-0 (` and `libqt6core6t64`, no `libglib2.0-0t64` and no `libxtst6 (>= 2:1.2.5)`);
     `pgrep -a qtranslate` empty; `sudo dpkg -i build/artifacts-linux/qtranslate_0.11.0+astra1_amd64.deb`;
     `dpkg -L qtranslate` (binary, desktop file, 256x256 icon); `desktop-file-validate
     /usr/share/applications/qtranslate.desktop`; `xvfb-run -a qtranslate --version`; a GUI start
     under `:0` through `gtk-launch qtranslate` with temporary XDG directories (record the window
     and stop the process); with `[app] autostart=true` under the temporary `XDG_CONFIG_HOME` the
     installed binary writes `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` with
     `Exec="/usr/bin/qtranslate" --minimized` and `autostart=false` removes it; the settings file is
     `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` and the verbose log
     `$XDG_STATE_HOME/qtranslate/qtranslate.log`; `sudo dpkg -r qtranslate` removes it and
     `dpkg -s qtranslate` reports not installed with `/usr/bin/qtranslate` gone. The installed
     `/usr/bin/qtranslate` is not in `/etc/X11/trusted.d/qtranslate`, so do not claim working
     global hotkeys from the installed package; the README documents the user's trusted-client step.
   - Trixie artifacts on Astra: `sudo dpkg -i build/artifacts-linux/qtranslate_0.11.0_amd64.deb` and
     capture the exact unsatisfied dependencies (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), then
     `sudo dpkg --purge qtranslate` and confirm it is gone; extract the trixie tar.gz and run
     `bin/qtranslate --version` with `LD_LIBRARY_PATH` unset, capturing `GLIBC_2.38` (Qt/ICU) and
     `CXXABI_1.3.15`; run the trixie AppImage with `--appimage-extract` and then
     `squashfs-root/AppRun --version`, capturing `GLIBC_2.38` and the `libsystemd` `GLIBC_2.39`
     lines. Do not edit `Depends`; no trixie artifact is claimed to run on Astra.
   - If `sudo` becomes unavailable or the user must run the package commands, write the exact
     commands and the reason into the Outcome and mark the install checks as not performed, never as
     passed. Leave no package installed.

8. Trixie: the three artifacts and the package metadata.
   - Identity: `(cd build/artifacts-linux && sha256sum -c SHA256SUMS-linux.txt)` is 3/3 OK and the
     hashes match the stage outcome (`bad8a16f…38f3` tar.gz, `677be95a…2e82` `.deb`,
     `e46dd917…1e45` AppImage). Confirm no source or CMake change after the artifact build commit:
     `git log --oneline 4b8ee83..HEAD -- src CMakeLists.txt tests` is empty, so the binaries in the
     artifacts are the HEAD code; the later `package.sh` changes are the Astra target, the shared
     ICU/imageformats derivation (which yields the same trixie result: ICU 76 and the three image
     plugins), and the CI wiring.
   - Layout and metadata: `tar -tzf` shows one top-level `qtranslate-0.11.0-linux-x86_64/` with
     `bin/qtranslate`, bundled Qt 6.8.2/ICU 76 in `lib/`, `plugins/`, `translations/` with
     `qt_de.qm`, `bin/qt.conf` (`Prefix = ..`, `Plugins = plugins`, `Translations = translations`),
     the desktop file, and the icon; `dpkg-deb --info`, `--contents`, and
     `--field Version,Depends,Installed-Size` show `0.11.0`, exactly the three installed files, no
     maintainer scripts, and `Depends` with `libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`,
     `libsecret-1-0`, and `libqt6dbus6` (trixie dependencies, no Ubuntu target).
   - Run them in a throwaway `debian:trixie` container (not the build image), mounting the artifact
     directory read-only and installing the `.deb` first so its dependencies are present:
     ```
     docker run --rm -v "$PWD/build/artifacts-linux:/art:ro" -w /tmp debian:trixie bash -lc '
       set -euo pipefail
       export DEBIAN_FRONTEND=noninteractive
       apt-get update
       apt-get install -y --no-install-recommends xvfb xauth desktop-file-utils ca-certificates
       apt-get install -y /art/qtranslate_0.11.0_amd64.deb
       xvfb-run -a qtranslate --version
       desktop-file-validate /usr/share/applications/qtranslate.desktop
       dpkg -L qtranslate
       tar -xzf /art/qtranslate-0.11.0-linux-x86_64.tar.gz -C /tmp
       env -u LD_LIBRARY_PATH xvfb-run -a /tmp/qtranslate-0.11.0-linux-x86_64/bin/qtranslate --version
       dpkg -r qtranslate
       xvfb-run -a /art/qtranslate-0.11.0-x86_64.AppImage --appimage-extract-and-run --version'
     ```
     Record the exact outputs, `ldd` on the extracted tar.gz binary (Qt/ICU resolve inside the
     extract), and that the AppImage runs without FUSE.
   - Record `grep -rn -i ubuntu packaging/ .github/workflows/build.yml` (the only matches are the
     `runs-on: ubuntu-latest` runner labels, not a build or package target) and confirm the Astra
     target is never used by CI (`grep -n astra .github/workflows/build.yml` is empty). The trixie
     image provides glibc 2.41, above the `GLIBC_2.38` the bundled Qt needs.

9. CI workflow.
   - `ruby -ryaml -e 'YAML.load_file(".github/workflows/build.yml"); puts "ok"'` prints `ok`
     (Ruby Psych; `on:` is read as a YAML 1.1 boolean key, expected).
   - Assert the `linux` job: `name: Linux x64 (trixie)`, `runs-on: ubuntu-latest`,
     `container: debian:trixie`, `DEBIAN_FRONTEND: noninteractive`, the pre-checkout `git` and
     `ca-certificates` step, `actions/checkout@v7`, installation from
     `packaging/linux/trixie-packages.txt` with the `grep -vE '^[[:space:]]*(#|$)'` form, Release
     configure with `-DCMAKE_PREFIX_PATH=/usr`, the build, exactly
     `timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure`,
     `packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux` without
     `--skip-appimage`, and the `qtranslate-linux-x64` upload with `if-no-files-found: error`.
   - Assert the `release` job: `needs: [windows, linux]`, both `download-artifact` steps into
     `artifacts`, the unchanged notes logic and `gh release upload artifacts/* --clobber`; the
     `windows` job, its Qt 6.8.3 install, its tag/version check, `on:`, `concurrency:`, and the
     top-level permissions are unchanged. Counters: `grep -c 'Verify tag matches'` is 1,
     `grep -n 'skip-appimage'` is empty, `grep -c 'qtranslate-linux-x64'` is 2.
   - Package list: `grep -vE '^[[:space:]]*(#|$)' packaging/linux/trixie-packages.txt | wc -l` is 21,
     `sort | uniq -d` is empty, and `packaging/linux/trixie.Dockerfile` contains no package names.
   - The local trixie run in step 6 used the same image and package list, but it is not a GitHub run:
     record the actual GitHub Actions run, the `v*` tag push, and the release publication as not
     performed (they need the user's explicit approval; nothing is pushed). Record
     `git log --oneline origin/main..HEAD` and the empty `v0.12*` tag list as proof.

10. Documentation audit (report only).
    - Read `README.md`, `README.ru.md`, `doc/architecture.md`, `doc/handoff.md`, the root
      `doc/roadmap/qtranslate/plan.md`, and `doc/roadmap/qtranslate/02-linux/plan.md`; do not edit
      any of them. Produce a table `file:line | current text | why it is stale | correct fact` and
      put it in the Outcome for the lead.
    - `README.md` / `README.ru.md` (must stay in sync; en and ru):
      - Status banner (`README.md:9-11`, `README.ru.md:9-11`): still says the Linux port is in
        development. The fact: Linux x86_64 is supported; X11 is the full target, Wayland is limited
        (portal where available, manual copy, CLI commands, warnings); 0.12.0 is not released yet,
        so do not invent Linux download links.
      - `## Download and install` (`README.md:47`, `README.ru.md:48`): there is no Linux
        installation. Missing: the trixie `.deb`, `tar.gz`, and AppImage names and install steps; the
        fact that the trixie artifacts do not run on Astra and that the Astra artifacts are local
        (not CI-published); that Linux assets appear only with the next release.
      - `## Usage` (`README.md:60`, `README.ru.md:61`): documents only `[--minimized] [--verbose]`.
        Missing `--translate-popup`, `--translate-main`, `--translate-inplace` (and desktop shortcut
        binding as the Wayland/no-hotkey fallback), and the Linux paths
        (`$XDG_CONFIG_HOME/qtranslate/qtranslate.ini`, `$XDG_STATE_HOME/qtranslate/qtranslate.log`);
        the page only names the Windows paths.
      - `### Linux (in progress)` (`README.md:145-164`, `README.ru.md:147-167`): says 20 of 20 tests
        and that hotkeys, selection capture, paste, and autostart are stubs — all false (28 tests, 20
        shared plus 8 platform tests; all implemented). Says CI builds Windows only — false (the
        `debian:trixie` job). Names `packaging/linux/trixie.Dockerfile` as the package list — the
        single list is `packaging/linux/trixie-packages.txt` (21 packages; the README's development
        apt line is a subset and misses `qt6-translations-l10n`, `dbus`, `desktop-file-utils`,
        `dpkg-dev`, `patchelf`, `qt6-wayland`, `file`, `curl` — sync it or point to the file).
        Check the Qt 6.5+ minimum and that trixie ships Qt 6.8.2;
        add the `packaging/linux/package.sh` build command; add the tray/GNOME AppIndicator note and
        the X11-full/Wayland-limited statement.
      - `#### Astra Linux: global hotkeys` (`README.md:166-188`, `README.ru.md:169-191`): check the
        installed path `/usr/bin/qtranslate`, the `fly-set-execid.sh` step, `getfattr` value
        `0sEA==` (`KBD_R`), the revoke commands, and that the attribute is lost on rebuild.
      - `## Known limitations` (`README.md:190-205`, `README.ru.md:193-208`): remove "Linux support
        is not ready yet"; add the real Linux limits: Wayland has no guaranteed global shortcuts or
        synthetic input (portal, manual copy, CLI, in-place copies to the clipboard), GNOME needs the
        AppIndicator extension for the tray, Astra needs the trusted-client step, the trixie
        artifacts are not for Astra, secrets use libsecret or the 0600 fallback with a warning, and
        the X11 clipboard restore does not keep non-text formats. Keep the Windows-only bullets
        (Win+V) clearly Windows-specific.
    - `doc/architecture.md`:
      - §1.3/§1.4 (`:38`, `:40`, `:42`, `:48-58`): "Linux x86_64 — planned", "Linux X11 (plan)",
        "Wayland (plan)", and the TLS/module wording; the port is implemented now, `Qt6::DBus` is a
        Linux module, and the platform matrix must show X11 as the full target and Wayland as
        limited.
      - §3.1/§3.2 tree (`:205-220`, `:229-256`, and the test list): still lists only `*_stub.cpp`
        and the 23 tests; missing `src/platform/*_linux.cpp`, `src/platform/sessiontype.h`,
        `portalshortcut.h`, `src/core/apppaths.h`, `src/app/commandline.*`, the eight platform test
        files, `packaging/linux/*`, and the workflow changes.
      - §3.3 build (`:275`): "for other platforms each module has a stub" is false; the Win32-only
        deploy block and the Linux install layout are missing.
      - §3.4 packaging/CI (`:329-341`): describes a Windows-only workflow; add the `linux` job
        (`debian:trixie`, `timeout 300 xvfb-run`, `package.sh`, `qtranslate-linux-x64`, the release
        job needing both), the trixie `package.sh` artifacts and `--target astra`, and the fact that
        only the trixie artifacts are CI-published.
      - §4.14 (`:1238-1241`, `:1244`, `:1272-1275`): "20 of 20 tests", "still stubs ... tasks
        02-04", "Planned artifacts", "CI — ubuntu-24.04, Qt 6.5+", and "the Windows platform tests
        ... the other 20 shared" are stale; the facts are 28 tests on Linux (20 shared + 8 platform
        tests), implemented backends, the actual artifact names, and CI in a `debian:trixie`
        container on an `ubuntu-latest` runner.
      - §8 test table (`:1483-1507`) and the manual checklist (`:1513-1517`): lists the 23 Windows
        tests only; add the eight Linux tests and their platform qualifiers, state 28 Linux/23
        Windows, and add the Linux X11 matrix (or a pointer to it). Optionally add the Linux
        verification record next to §8.4; that is the lead's choice.
      - §9.1 `CI and releases` row (`:1638`) and §9.2 (`:1657`, `:1671`): "GitHub Actions on
        `windows-latest`", "There is no Linux build in 0.11.0: platform modules are stubs" (true for
        0.11.0, but the stage-2 state must not read as current), and item 5 "Linux — roadmap stage 2".
    - `doc/handoff.md`: the header date/repository (`:3`, still `C:\projects\qtranslate`), the
      current state (stage 2 gate, 28 tests, packaging, CI not run, 0.12.0 pending), and the "Linux
      baseline" section (`:36-135`): `package.sh` is described only as the portable archive, the
      task-07 list and the "children 01-02 done, child 03 not started" sentence are stale, the
      "READMEs still say 20/20 (task 08)" note is stale after this task, and the final gate's
      not-performed list and known limitations belong there.
    - Map: the root `plan.md` and the stage `02-linux/plan.md` must not claim a not-performed result;
      the stage status/outcome is the lead's. Do not change the stage `## Done when`.
    - Also report (out of this node's explicit list, the lead decides): `AGENTS.md` still says the
      Linux port is planned and `src/platform/*_stub.cpp` are stubs, "all 23 tests must pass",
      "`tst_hotkeymanager`, `tst_autostart` and `tst_secretprotector` are Windows-only", the build
      helper hardcodes `C:\projects\qtranslate`, and the CI description is Windows-only. The
      `package.sh` `--help` and the workflow are the sources of truth for the correct facts.
    - Do not edit any document; the lead owns `doc/`, the READMEs, `AGENTS.md`, and the map.

11. Hygiene and the report.
    - `git status --short` shows only this plan file; `git diff --check` is clean; `git diff
      --stat` has no source, test, CMake, packaging, or documentation file. Do not commit, push,
      tag, or publish unless the lead asks.
    - No leftovers: `pgrep -a qtranslate` empty; `dpkg-query -W -f='${Status}' qtranslate` not
      installed; no Weston or stray `kate`/`Xvfb` processes; no root-owned files in the repository
      (`find . -user root -not -path './.git/*'` empty); temporary trees and screenshots under
      `/tmp/opencode/verify-linux/`; record any host package installed for the check
      (`wl-clipboard`, if used) and remove it unless the user wants to keep it.
    - Log hygiene: `grep -R -n -E 'Hello|Hallo|Authorization|apiKey|api_key|sk-[A-Za-z0-9]'
      /tmp/opencode/verify-linux/` finds nothing in the application logs; the logs contain no
      translated text, API keys, or passwords.
    - Fill `## Outcome` with the traceability table (every stage `## Done when` bullet → step and
      evidence), the host and tool versions, the test counts and per-test counts, the artifact
      names/sizes/SHA256, the exact `Depends`, the screenshot and log paths, the documentation gap
      table, the not-performed list with reasons (Windows Debug/Release and the 23 tests, the real
      `GlobalShortcuts` portal, the live Wayland clipboard/in-place if it could not run, the real
      GitHub run/tag/release, the trixie keyring round-trip skip, any `:0` item that could not be
      observed), and the resulting state. The lead commits the map and the journal; this task stops
      after the report.

## Done when

- The environment and baseline are recorded: HEAD hash, host and tool versions, the image ID, the
  empty start state (no running instance, no installed package), and the evidence directory.
- Astra Linux: Debug and Release build at HEAD with `-Wall -Wextra` and zero warnings; CTest 28/28 in
  both (after `sudo /usr/bin/fly-set-execid.sh`), with `tst_hotkeymanager_x11` 16/16 and
  `tst_hotkey_selection_x11` 7/7 and no skip; the per-binary counts and `tst_secretprotector` 8/8
  are recorded; `qtranslate --version` prints `QTranslate 0.11.0`; the application starts under the
  real `:0` session (window observed, no process or window left behind).
- Debian 13 (trixie): Debug and Release configure and build at HEAD with Qt 6.8.2 and
  `-Wall -Wextra`, zero `warning:` lines; `timeout 300 xvfb-run -a ctest` is 28/28 in both; the
  application prints its version under `xvfb-run`; the per-binary counts are recorded and the
  `tst_secretprotector` keyring case is recorded as not performed there (covered on Astra).
- X11 matrix on the real `:0`: Ctrl+Q shows the popup with the selection (screenshot reviewed),
  Ctrl+Shift+Q replaces it (file content), Ctrl+W fills the main window; the tray menu's Show window,
  Pause hotkeys, Service, Settings, and Quit work; settings open, save, and persist; autostart
  writes and removes `$XDG_CONFIG_HOME/autostart/qtranslate.desktop`; the three CLI commands work
  with and without a running instance; a second instance forwards and exits 0; the clipboard is
  restored (sentinel check). Each item has a log excerpt and a screenshot or file evidence, or is
  recorded as not performed with the exact reason.
- Wayland: Weston headless starts the application, the log carries the missing
  `GlobalShortcuts` portal reason and the CLI fallback, `strace` shows no X11 connection, no
  `ydotool`/`wtype` or synthetic input exists; the `Limited Wayland support` warning is captured (or
  the visual confirmation is recorded as not performed with the reason); the manual-copy/in-place
  clipboard path is verified with `wl-copy`/`wl-paste` or recorded as not performed with the exact
  reason and the unit-test coverage (`tst_selectiongrabber_linux`, `tst_windowactivation_linux`);
  `tst_hotkeymanager_wayland` is 20/20; a real portal desktop is not performed with the reason.
- Astra artifacts: the Astra `tar.gz` runs on Astra with no Qt in `LD_LIBRARY_PATH` (Qt 6.10.2/ICU 72
  resolve inside, `patchelf` RPATH, `desktop-file-validate`); the Astra `.deb` (`0.11.0+astra1`)
  installs with `dpkg -i`, `qtranslate --version` and the GUI start work, the desktop entry/icon,
  autostart, settings, and log paths are checked, and `dpkg -r` removes it. The installed binary is
  not claimed to have working hotkeys without the documented trusted-client step.
- Trixie artifacts: the `tar.gz`, `.deb`, and AppImage are checked on trixie (AppImage with
  `--appimage-extract-and-run`); the `.deb` installs/removes and its `Depends` show the trixie
  dependencies (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`); the contents contain the bundled Qt
  6.8.2, plugins, translations, `qt.conf`, desktop file, and icon; `SHA256SUMS-linux.txt` verifies
  3/3; no Ubuntu target remains. The trixie `.deb`, `tar.gz`, and AppImage fail on Astra with the
  exact dependency and `GLIBC_2.38`/`CXXABI_1.3.15`/`libsystemd` `GLIBC_2.39` lines, and neither is
  claimed to work there.
- CI: the workflow parses as YAML and the `linux` job (trixie container, package list, Release
  build, `timeout 300 xvfb-run -a ctest`, `package.sh` without `--skip-appimage`,
  `qtranslate-linux-x64`) and the `release` job (`needs: [windows, linux]`, both artifact sets) match
  the contract; the Windows job, tag check, `on:`, and `concurrency:` are unchanged; the real
  GitHub run, `v*` tag, and release publication are recorded as not performed with the reason.
- Documentation: every listed item of `README.md`, `README.ru.md`, `doc/architecture.md`,
  `doc/handoff.md`, the root plan, and the stage is checked; gaps are reported with `file:line` and
  the correct fact; no documentation file is edited by this task.
- Hygiene and report: `git diff --check` is clean, only this plan file is modified, no code/test/
  packaging/documentation file changed; no running instance, installed package, root-owned file, or
  stray compositor/window is left; the logs contain no translated text, API keys, or passwords; the
  Outcome contains the traceability table, the evidence paths, the documentation gap table, and
  every not-performed check with its reason; no Windows or GitHub result is claimed that did not
  run.

## Risks

- The `:0` session is the user's live desktop. Use a temporary `HOME`/`XDG_*`, keep windows on
  synthetic content only, close every window and process, and never change the user's settings. If
  the session is in use or unavailable, run the scripted `xvfb-run` pass and record the real-session
  item as not performed; do not claim a real-session pass.
- The translation scenarios need a provider. With a fresh temporary config the default is the free
  Google endpoint; it can answer `429`. Fall back once to DeepL `free-web`, record the provider and
  the exact response, and if no translation succeeds, record the scenario as not performed rather
  than describing a window that never showed a result.
- Astra's XPARSEC `KBD_R` attribute is lost on every rebuild, and the installed `/usr/bin/qtranslate`
  is not in `/etc/X11/trusted.d/qtranslate`. Re-run `fly-set-execid.sh` before the tests, use the
  build-tree binary for the hotkey matrix, and do not claim hotkeys for the installed package
  without the user's documented step. A fresh build tree would `QSKIP` the two X11 tests; a skip is
  not a pass.
- `dpkg -i` of the trixie `.deb` leaves the package unpacked but not configured; purge it before
  installing the Astra package and leave no package installed. If `sudo` stops working, record the
  exact commands and mark the install checks as not performed.
- Weston headless has no input seat, so it may deliver no clipboard offer; the nested X11 backend
  gives focus but is a second compositor on the user's session. Try both, record the exact failure,
  and never substitute a claim for the live check; the unit tests are the stated fallback evidence.
- This plan does not rebuild the trixie artifacts: the accepted set came from the packaging stage and
  its acceptance, no source or CMake file changed after it, and the artifacts are not
  byte-reproducible. If a fresh build is ever needed, run `package.sh` in the `/tmp/opencode` copy
  (never as root against the repository) and record the new names and hashes. Treat
  `qtranslate-trixie:latest` as a moving tag and record its ID (`b17f22fb75d9` here).
- The artifacts are not byte-reproducible and the preserved set predates `25c0f49` and `d0f4b37`
  (`package.sh` only; the shared ICU/imageformats derivation gives the same trixie result); verify
  the source/CMake timeline before treating the artifacts as HEAD code, and compare names, contents,
  and `Depends` rather than hashes across runs.
- The AppImage `ldd` gate checks `not found` only, plugin presence is per directory, the Astra host
  gate checks `ID=astra` only, and the Astra `patchelf` came from the Debian pool; these are known
  limitations, not new defects, but they must appear in the Outcome rather than be hidden.
- `ctest` on the live `:0` fails two focus tests by design; the gate is `xvfb-run`. Do not "fix" the
  test environment or the settings to make a bare `ctest` pass.
- Screenshots of the user's desktop can capture private content: capture only the QTranslate window
  (or crop to the panel for the tray), keep them under `/tmp/opencode/verify-linux/`, and delete them
  if the lead does not need them.
- The real GitHub run, tag, and release need the user's explicit approval; this task must not push,
  tag, or publish. Windows Debug/Release and the 23 tests cannot run here; record them as not
  performed.
- The gate is long. If the context or the session runs out, stop after a complete step, leave the
  evidence directory intact, and record in the Outcome which steps were completed and which were
  not; the next session continues from the uncompleted step.

## Outcome

The final Linux-stage gate was run on 2026-11-08 at HEAD
`0a3a7628e5fc9f49b24648d9af6aba2550688f15` (branch `main`, seven local commits ahead of
`origin/main`, no `v0.12*` tag). Every stage `## Done when` item is covered; the checks that could
not be run here are listed below as not performed with the reason. No code, test, CMake, packaging,
or documentation file was changed: only this plan and the journal are modified. All evidence is
under `/tmp/opencode/verify-linux/`; paths below are relative to it.

### Baseline and environment

- Host: Astra Linux 1.8.7 (`/etc/os-release`: `ID=astra`, `VERSION_ID=1.8_x86-64`), Qt 6.10.2, GCC
  12.2.0 (`Astra 12.2.0-14.astra6+deb12u1`), CMake 3.31.6, glibc 2.36, Weston 10.0.1. `xvfb-run`,
  `xdotool`, `xclip`, `xsel`, `scrot`, `import`, `strace`, `getfattr`, `gtk-launch`, `kate`,
  `patchelf`, `rsync`, and Ruby Psych are present; `sudo -n true` succeeds. Start state: no
  `qtranslate` process, package not installed (`step1-baseline.log`).
- Container: `qtranslate-trixie:latest` = `b17f22fb75d9` on `debian:trixie` `51e269e6af18`; Qt
  6.8.2, GCC 14.2.0; the image is the same 21-package environment as the CI job
  (`trixie-docker.log`, `trixie-artifacts/metadata.log`).
- The start working tree also held the planner's journal entry (`step1-baseline.log`); this task
  added the plan body, the Outcome, and the journal update only.

### Traceability: stage `## Done when` → step → evidence

| Stage criterion | Step | Evidence |
|---|---|---|
| Environment and baseline recorded: HEAD, host/tool versions, image ID, empty start state, evidence directory | 1 | `step1-baseline.log`: HEAD `0a3a762…`, seven local commits, empty `v0.12*` tag list, `ID=astra`/`1.8_x86-64`, Qt 6.10.2, GCC 12.2, CMake 3.31.6, glibc 2.36, images `b17f22fb75d9`/`51e269e6af18`, Weston 10.0.1, tool paths, `sudo passwordless ok`, no process, no package |
| Astra: Debug/Release build at HEAD with `-Wall -Wextra` and zero warnings; CTest 28/28 in both after `fly-set-execid.sh`; X11 tests 16/16 and 7/7 with no skip; per-binary counts and `tst_secretprotector` 8/8 recorded; `--version` and a real-`:0` start with no leftovers | 2, 5 | `astra-{debug,release}-configure.log` and `astra-{debug,release}-build.log` (no `warning:` lines); `astra-{debug,release}-ctest.log` end with `100% tests passed, 0 tests failed out of 28`; `astra-per-test.log` records 16, 20, 12, 13, 7, 6, 8, 20 and `skip-lines=0`; `exec-id-getfattr.log` shows `security.exec_id=0sEA==` for the Release X11 binaries; `smoke2b/version-xvfb.log` is `QTranslate 0.11.0`; `smoke2b/xwininfo-tree.txt` shows the `QTranslate` 1640x880 window, `smoke2b/window-ids-after.txt` is empty |
| Trixie: Debug/Release configure/build at HEAD with Qt 6.8.2 and zero `warning:` lines; `timeout 300 xvfb-run -a ctest` 28/28 in both; version under `xvfb-run`; per-binary counts; keyring case recorded as not performed | 6 | `trixie-src/build/verify-logs/{cmake,build,ctest,version}-{Debug,Release}.log`; both CTest logs end with `100% tests passed, 0 tests failed out of 28`; the build logs have 0 `warning:` lines; `trixie-per-test.log` records 16, 20, 12, 13, 7, 6, 7 passed + 1 skipped, 20; `version-*.log` is `QTranslate 0.11.0` |
| X11 matrix on the real `:0`: Ctrl+Q popup with the selection (screenshot reviewed), Ctrl+Shift+Q replaces it, Ctrl+W fills the main window; tray Show window/Pause/Service/Settings/Quit; settings open/save/persist; autostart create/remove; three CLI commands with and without an instance; second instance forwards and exits 0; clipboard restored | 3 | `x11/ctrlq-log.txt` (`Hotkey activated: 0`, `Popup shown ... after Ctrl+Q`, Google HTTP 200), `x11/x11-ctrlq.png` (popup shows `Hallo Welt` and `Zweite Zeile bleibt.`), `x11/ctrlq-clipboard.txt` (`QTV-clip-sentinel`); `x11/ctrlshiftq-file.txt` (first line replaced, second kept) and `x11/ctrlshiftq-log.txt` (`Hotkey activated: 1`); `x11/ctrlw-windows.txt` (1640x880 viewable main window), `x11/x11-ctrlw.png`, `ctrlw-log.txt` (`Hotkey activated: 2`); tray `x11/x11-tray-*.png`, `x11/tray-window.txt`, log `Hotkeys suspended`/`Hotkeys resumed`; settings `x11/x11-settings-*.png`, `x11/config/qtranslate/qtranslate.ini`; autostart `x11/autostart-enabled.desktop`, log `Autostart entry updated`/`removed`; CLI `x11/cli-*.{out,err,log}`, `x11/cli-received.log`, `x11-cli-*.png`; activation `x11/cli-activate-*` and log `Received command "activate"` |
| Wayland: Weston headless starts the app, the log carries the missing `GlobalShortcuts` portal reason and CLI fallback, `strace` shows no X11 connection, no `ydotool`/`wtype`; the `Limited Wayland support` warning is captured or recorded as not visually confirmed; live clipboard/in-place verified or recorded as not performed with the exact reason and unit coverage; `tst_hotkeymanager_wayland` 20/20; a real portal desktop is not performed | 4 | `wayland/weston-headless.log` (Weston 10.0.1, headless backend); `wayland/app-log.txt` has `GlobalShortcuts portal unavailable: "No D-Bus session bus is available, ..."` and the `Use a CLI command or a desktop shortcut ... fallback` sentence, and `Started in 88 ms`; `wayland/wayland-connect.log` connects only to the private `wayland-verify` socket and `/nonexistent`, no `/tmp/.X11-unix`; `src/` has no `ydotool`/`wtype` match; `wayland/wayland-warning-limited.png` shows the `Ограниченная поддержка Wayland` dialog; live clipboard/in-place not performed (`wayland/wl-popup-log.txt`: `Read 0 characters from the clipboard`, `Popup shown (nothing selected)`); `tst_selectiongrabber_linux` 12/12 and `tst_windowactivation_linux` 13/13; `tst_hotkeymanager_wayland` 20/20 |
| Astra artifacts: the tar.gz runs with no Qt in `LD_LIBRARY_PATH` (Qt 6.10.2/ICU 72 inside, RPATH, `desktop-file-validate`); the `0.11.0+astra1` `.deb` installs, `--version` and GUI start work, desktop entry/icon, autostart, settings, log paths checked, `dpkg -r` removes it; the installed binary is not claimed to have hotkeys without the trusted-client step | 7 | `astra-artifacts/hashes.log` (2/2 checksums OK), `astra-artifacts/tar-version.log` (`QTranslate 0.11.0`), `astra-artifacts/tar-rpath-ldd.log` (`libs=28 plugins=8 bad=0`), `desktop-file-validate` rc 0 on the bundled file; `astra-artifacts/deb-metadata.log` (`Version: 0.11.0+astra1`, `Depends` with `libglib2.0-0 (>= 2.12.0)` and `libxtst6`, no `t64`/floor, `Installed-Size: 1396`, three files); `astra-artifacts/deb-installed.log` (`dpkg -L`, validate, `--version`, not in `trusted.d`); `astra-artifacts/gui/windows.txt` (1640x880 window), `gui/autostart-enabled.desktop` (`Exec="/usr/bin/qtranslate" --minimized`), `gui/config/qtranslate/qtranslate.ini`; `dpkg-query` not installed afterwards (`step1-baseline.log` and the final hygiene check) |
| Trixie artifacts: tar.gz, .deb, AppImage checked on trixie (AppImage with `--appimage-extract-and-run`); .deb installs/removes with trixie `Depends` (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`); contents have bundled Qt 6.8.2, plugins, translations, `qt.conf`, desktop file, icon; `SHA256SUMS-linux.txt` 3/3; no Ubuntu target; the trixie artifacts fail on Astra with the exact lines and are not claimed there | 8 | `trixie-artifacts/metadata.log` (hashes `bad8a16f…38f3`, `677be95a…2e82`, `e46dd917…1e45`; 3/3 OK; empty source timeline; tar layout; `qt.conf`; Qt 6.8.2; `Depends` with `libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`, `libsecret-1-0`, `libqt6dbus6`; three files); `trixie-artifacts/trixie-container-run.log` (deb install/version/validate/remove; tar version; tar `ldd` resolves Qt/ICU inside); `trixie-artifacts/trixie-container-appimage.log` (`QTranslate 0.11.0`, no FUSE); `trixie-artifacts/ubuntu-references.log` (only the two `runs-on: ubuntu-latest` labels); `trixie-artifacts/astra-workflow.log` empty; `astra-artifacts/trixie-deb-on-astra.log` (`libglib2.0-0t64` missing, `libxtst6` 2:1.2.3 vs `>= 2:1.2.5`), `astra-artifacts/trixie-tar-on-astra.log` (`GLIBC_2.38`, `CXXABI_1.3.15`), `astra-artifacts/trixie-appimage-on-astra.log` (`GLIBC_2.38`, `libsystemd` `GLIBC_2.39`) |
| CI: YAML parses; the `linux` job (trixie container, package list, Release, `timeout 300 xvfb-run -a ctest`, `package.sh` without `--skip-appimage`, `qtranslate-linux-x64`) and the `release` job (`needs: [windows, linux]`, both artifact sets) match; the Windows job, tag check, `on:`, `concurrency:` unchanged; the real GitHub run/tag/release recorded as not performed | 9 | `ci/workflow-checks.log` (`YAML parse ok`; markers at the expected lines; counters `verify-tag-count=1`, `skip-appimage-lines=0`, `linux-artifact-count=2`; `packages=21`, `duplicates=0`; no package names in `trixie-packages.txt`-consuming Dockerfile); `ci/workflow-unchanged.log` (`identical (windows job, on:, permissions:, concurrency:)`, `identical (release notes/upload logic)`, `needs: [windows, linux]`); `ci/workflow-before*.yml`/`workflow-head*.yml`; `ci/release-before.txt`/`release-head.txt` |
| Documentation: every listed item checked; gaps reported with `file:line` and the correct fact; no documentation file edited | 10 | The gap table below; `git diff --stat` shows only this plan and the journal; `git diff --check` is clean |
| Hygiene and report: `git diff --check` clean, only this plan modified, no code/test/packaging/documentation change; no process/package/root-owned file/stray compositor or window; logs clean; the Outcome contains the traceability table, evidence paths, gap table, and every not-performed check with its reason | 11 | Final checks: `git status --short` only this plan and the journal; `git diff --check` clean; `pgrep -a qtranslate` empty; `dpkg-query` no package; no Weston/Xvfb/kate; `find . -user root -not -path './.git/*'` empty; clipboard md5 `0d111f56…` matches `x11/user-clipboard-original.txt`; `wl-clipboard` removed |

### X11 matrix details (step 3)

- **Ctrl+Q**: Google free endpoint answered `HTTP 200`; the popup shows `Hallo Welt` and
  `Zweite Zeile bleibt.`; the clipboard sentinel `QTV-clip-sentinel` was restored. Screenshot
  `x11/x11-ctrlq.png`; log `x11/ctrlq-log.txt`.
- **Ctrl+Shift+Q**: `kate` saved the file with the first line replaced by `Hallo Welt` and the
  second line intact (`x11/ctrlshiftq-file.txt`); the sentinel was restored; log
  `x11/ctrlshiftq-log.txt` has `Hotkey activated: 1`.
- **Ctrl+W**: the main window (1640x880) became viewable; `alt+F4` closed it and the process stayed
  in the tray; log `Hotkey activated: 2`; screenshots `x11/x11-ctrlw.png`,
  `x11/ctrlw-windows.txt`.
- **Tray**: the panel and menu screenshots are `x11/x11-tray-panel.png`,
  `x11/x11-tray-menu.png`, `x11/x11-tray-menu-before-pause.png`, `x11/x11-tray-menu-paused.png`,
  `x11/x11-tray-service.png`, `x11/x11-tray-service-submenu.png`, `x11/x11-tray-menu-quit.png`.
  Pause logged `Hotkeys suspended` and suppressed the popup; resume re-registered all three and
  logged `Hotkeys resumed`; Show window opened the main window; Settings opened the dialog; Quit
  ended the process.
- **Settings**: the dialog was opened, `Restore clipboard` was cleared and accepted
  (`x11/x11-settings-changed.png`), the application restarted, the INI re-read, and the setting
  restored (`x11/x11-settings-restored.png`, `x11/config/qtranslate/qtranslate.ini`).
- **Autostart**: `x11/autostart-enabled.desktop` has `Type=Application`,
  `Exec="/home/parsee/sync/mine/QTranslate/build/linux-release/qtranslate" --minimized`,
  `Icon=qtranslate`, `X-GNOME-Autostart-enabled=true`; the log has `Autostart entry updated` and
  `Autostart entry removed`.
- **CLI**: without an instance, `--translate-popup`, `--translate-main`, and
  `--translate-inplace` started the popup/main/in-place path and exited 0 with one process left
  (`x11/cli-*-noinstance.*`, `x11/x11-cli-popup-noinstance.png`,
  `x11/x11-cli-main-noinstance.png`); those runs had no selection, so the windows show
  "nothing selected" — command delivery is the evidence, while the translated content path is the
  Ctrl+Q/Ctrl+W matrix. With an instance, each command produced one `Received command` line
  (`x11/cli-received.log`) and the secondary exited 0; `--minimized --translate-popup` stayed in
  the tray; `--translate-popup --translate-main` and `--bogus` exited 1 with the expected messages
  (`x11/cli-conflict.err`, `x11/cli-bogus.err`); `--help` lists the three options
  (`x11/cli-help.out`); a plain start forwarded `activate` and exited 0.
- The user's real clipboard was saved to `x11/user-clipboard-original.txt` before the matrix and
  restored afterwards (md5 match in the final hygiene check).

### Wayland details (step 4)

- Weston 10.0.1 headless started with the private socket; the application stayed alive with
  `GlobalShortcuts portal unavailable` and the CLI/desktop-shortcut fallback sentence
  (`wayland/app-log.txt`). `strace` shows no X11 connection (`wayland/wayland-connect.log`).
- The nested Weston X11 run produced the `Ограниченная поддержка Wayland` dialog
  (`wayland/wayland-warning-limited.png`), so the warning is visually confirmed.
- `wl-clipboard` was installed for the live check and removed afterwards; the nested Weston seat
  delivered no selection offer (`Read 0 characters from the clipboard`), so the live
  clipboard/in-place check is not performed; `tst_selectiongrabber_linux` 12/12 and
  `tst_windowactivation_linux` 13/13 cover the Wayland clipboard read and the no-paste path.
- `tst_hotkeymanager_wayland` 20/20 is the portal gate; a real KDE/GNOME `GlobalShortcuts` desktop
  is not performed (Astra runs X11; Weston has no portal).

### Astra artifact details (step 7)

- Checksums: tar.gz `f8e5d7e42a5eb13008cf66caa1cce286ec6242619c9cba0cc42140a6bf77e80d`
  (27 289 681 bytes), `.deb`
  `6c2f24bbe00f5cea5c57fda5c529808d7b1fd93fabbb234283ea97635b9f797f` (403 056 bytes);
  `sha256sum -c SHA256SUMS-linux-astra.txt` 2/2.
- The tar.gz `--version` is `QTranslate 0.11.0`; `ldd` on the binary, `lib/`, and plugins reports
  28 libraries and 8 plugins with `bad=0`; `patchelf --print-rpath` is `$ORIGIN` on
  `libQt6Core.so.6` and `$ORIGIN/../../lib` on `libqoffscreen.so`, and `QT_DEBUG_PLUGINS=1` loads
  the bundled offscreen plugin (`astra-artifacts/tar-rpath-plugin.log`); `desktop-file-validate`
  passes.
- The `.deb` `Depends` is `libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0 (>= 2.12.0),
  libqt6core6t64 (>= 6.10.2), libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2),
  libqt6network6 (>= 6.7.2), libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5),
  libx11-6, libxtst6` — no `libglib2.0-0t64` and no `libxtst6 (>= 2:1.2.5)`.
- `dpkg -i`/`-L`, the desktop file, `xvfb-run -a qtranslate --version`, the real-`:0` `gtk-launch`
  start (1640x880 window), autostart create/remove with `Exec="/usr/bin/qtranslate" --minimized`,
  and `dpkg -r` passed; `dpkg -s` reports not installed afterwards. The installed binary is not in
  `/etc/X11/trusted.d/qtranslate`, so no working hotkeys are claimed for it; the GUI stderr shows
  the expected `The hotkey is already used by another program` warnings
  (`astra-artifacts/gui/gtk-launch.err`).

### Trixie artifact details (step 8)

- Checksums: tar.gz `bad8a16f112d42d7c2029ebe9012654e0e8237d43db3d9a70e9851b05cc238f3`
  (28 636 427 bytes), `.deb`
  `677be95a57256ec37005a77c134a280b231235c698a42e382ae54f94cbbe2e82` (406 188 bytes), AppImage
  `e46dd917b5c8cedbd6f3520774ba3b90b2ce7eb6df52e880186f120416511e45` (39 270 904 bytes);
  3/3 OK.
- The tar.gz has one top-level directory, bundled Qt 6.8.2 and ICU 76, `plugins/`,
  `translations/qt_de.qm`, `bin/qt.conf` (`Prefix = ..`, `Plugins = plugins`,
  `Translations = translations`), the desktop file, and the icon.
- The `.deb` is `Version: 0.11.0`, has exactly the three installed files and no maintainer
  scripts; `Depends` is `libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0t64 (>= 2.12.0),
  libqt6core6t64 (>= 6.8.2), libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2),
  libqt6network6 (>= 6.7.2), libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5),
  libx11-6, libxtst6 (>= 2:1.2.5)`.
- In a clean `debian:trixie` the `.deb` installs, `qtranslate --version` and
  `desktop-file-validate` pass, `dpkg -L` lists the three files, and `dpkg -r` removes it; the
  tar.gz runs with `LD_LIBRARY_PATH` unset and its `ldd` resolves Qt/ICU inside the extract; the
  AppImage runs with `--appimage-extract-and-run --version` without FUSE. The trixie image
  provides glibc 2.41, above the bundled Qt's `GLIBC_2.38`.
- **Plan correction:** the step 8 command originally extracted and ran the tar.gz before installing
  the `.deb`; in a clean `debian:trixie` that fails on `libXtst.so.6` (a `.deb` dependency). The
  command in `## Steps` now installs the `.deb` first; the verification was run with that order
  (`trixie-artifacts/trixie-container-run.log`, `trixie-artifacts/trixie-container-appimage.log`).
  The first clean-container attempt also aborted its remove check on a shell-quoting typo
  (`Status: unbound variable`); the follow-up run verified the removal (`dpkg-query: no packages
  found matching qtranslate`).
- `grep -rn -i ubuntu packaging/ .github/workflows/build.yml` finds only the two
  `runs-on: ubuntu-latest` labels; `grep -n astra .github/workflows/build.yml` is empty. No
  Ubuntu build target and no CI Astra target remain.

### CI details (step 9)

The workflow parses with Ruby Psych (`ok`); the `Linux x64 (trixie)` job has `runs-on:
ubuntu-latest`, `container: debian:trixie`, `DEBIAN_FRONTEND: noninteractive`, the pre-checkout
`git`/`ca-certificates` step, `actions/checkout@v7`, the `trixie-packages.txt` filter, a Release
configure with `-DCMAKE_PREFIX_PATH=/usr`, `timeout 300 xvfb-run -a ctest --test-dir build/ci
--output-on-failure`, `package.sh` without `--skip-appimage`, and the `qtranslate-linux-x64` upload
with `if-no-files-found: error`. The `release` job is `needs: [windows, linux]` and downloads both
artifact sets into `artifacts`; the Windows job, its Qt 6.8.3 setup, the tag/version check, `on:`,
`permissions:`, and `concurrency:` are identical to the previous workflow. Counters:
`Verify tag matches` = 1, `skip-appimage` = 0 lines, `qtranslate-linux-x64` = 2; the package list
has 21 packages, no duplicates, and `trixie.Dockerfile` contains no package names.

### Documentation gaps (report for the lead; no document was edited)

`en`/`ru` READMEs must stay in sync; the Astra section was checked and is accurate.

| File:line | Current text (abridged) | Why stale | Correct fact |
|---|---|---|---|
| `README.md:11`, `README.ru.md:11` | "Linux support is in development." | Linux x86_64 is implemented at HEAD; only the 0.12.0 release is pending. | Linux x86_64 is supported from 0.12.0: X11 full; Wayland limited (`GlobalShortcuts` portal where available, manual copy, CLI commands, warnings); no Linux download links until the release. |
| `README.md:47`, `README.ru.md:48` | Download lists only the Windows setup and ZIP. | No Linux artifact names, install steps, or the Astra/trixie split. | Add the trixie `.deb`/`tar.gz`/AppImage (AppImage with `--appimage-extract-and-run` without FUSE), the native Astra `-astra.tar.gz` and `+astra1_amd64.deb` as local (not CI-published); Linux assets appear with the next release. |
| `README.md:60-63`, `README.ru.md:61-64` | `qtranslate [--minimized] [--verbose]` | Missing the three Linux CLI commands, the desktop-shortcut fallback, and the Linux paths. | Add `--translate-popup`, `--translate-main`, `--translate-inplace` (shortcut binding as the Wayland/no-hotkey fallback), `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini`, `$XDG_STATE_HOME/qtranslate/qtranslate.log`. |
| `README.md:145-164`, `README.ru.md:147-167` | "20 of 20 tests"; "platform modules are still stubs"; "CI currently builds Windows only"; "package list matches `packaging/linux/trixie.Dockerfile`" | False at HEAD: 28 tests, implemented backends, a `debian:trixie` CI job, and the single list is `trixie-packages.txt`. | 28 Linux tests (20 shared + 8 platform: `tst_hotkeymanager_x11` 16, `tst_hotkeymanager_wayland` 20, `tst_selectiongrabber_linux` 12, `tst_windowactivation_linux` 13, `tst_hotkey_selection_x11` 7, `tst_autostart` 6, `tst_secretprotector` 8 on Astra/7+1 on trixie, `tst_cli` 20); X11 full/Wayland limited; add the `package.sh` build command and the GNOME AppIndicator note; trixie Qt 6.8.2 satisfies Qt 6.5+. The development apt line is a subset of the 21-package list and misses `qt6-wayland`, `qt6-translations-l10n`, `dbus`, `desktop-file-utils`, `dpkg-dev`, `patchelf`, `curl`, `file` (plus `git`/`ca-certificates` for CI). |
| `README.md:166-188`, `README.ru.md:169-191` | Astra trusted-client section. | Checked: matches the verified `0sEA==`/`KBD_R` procedure and the "/usr/bin/qtranslate" installed path. | No change required. |
| `README.md:190-205`, `README.ru.md:193-208` | "Linux support is not ready yet; see the build section above." | False; the port is implemented, only the release is pending. | Replace with the real limits: Wayland has no guaranteed global shortcuts or synthetic input (portal, manual copy, CLI, in-place copies to the clipboard); GNOME needs the AppIndicator extension; Astra needs the trusted-client step; trixie artifacts are not for Astra; secrets use libsecret or the 0600 fallback with a warning; the X11 restore does not keep non-text formats. Keep the Win+V bullet clearly Windows-specific. |
| `doc/architecture.md:38-42` (§1.3) | "Linux: GCC/Clang — roadmap stage 2", "Linux x86_64 — planned", "on Linux the Qt OpenSSL backend will be used". | The Linux port is implemented; the real `:0` matrix made a working HTTPS request to Google on Astra. | Linux is current: GCC 12.2/Astra and 14.2/trixie, Qt 6.10.2/6.8.2, the Qt OpenSSL backend is in use; add `Qt6::DBus` as a Linux module. |
| `doc/architecture.md:48-58` (§1.4) | Platform matrix header "Linux x86_64 (planned, roadmap stage 2 (old stage 11))". | The matrix describes implemented code. | Header "Linux x86_64 (0.12.0)" with X11 as the full target and Wayland as limited; the rows already match the implementation. |
| `doc/architecture.md:205-220`, `:229-256`, `:266` (§3.1/§3.2) | Platform tree lists only `*_stub.cpp`; test list has 23 tests and `tst_hotkeymanager (Windows)`. | Missing the Linux files and eight platform tests; `qtranslate` also links `Qt6::DBus` on Linux. | Add `src/platform/*_linux.cpp`, `sessiontype.h`, `portalshortcut.h`, `src/core/apppaths.h`, `src/app/commandline.*`; add the eight Linux tests (`tst_hotkeymanager_x11`, `tst_hotkeymanager_wayland`, `tst_selectiongrabber_linux`, `tst_windowactivation_linux`, `tst_hotkey_selection_x11`, `tst_cli`, plus the Linux `tst_autostart`/`tst_secretprotector`); state 28 Linux/23 Windows. |
| `doc/architecture.md:275-282` (§3.3) | "for other platforms each module has a stub". | False: the Linux backends are real; the stubs are only for platforms without an implementation. | Name the Linux backends, the Win32-only deploy block, the Linux install layout (`bin/qtranslate`, desktop file, hicolor icon), and `Qt6::DBus` on Linux. |
| `doc/architecture.md:329-341` (§3.4) | CI described as a Windows-only workflow on `windows-latest`. | The workflow now has a Linux job and the release needs both. | Add the `linux` job (`debian:trixie` container on `ubuntu-latest`, 21-package list, `timeout 300 xvfb-run -a ctest`, `package.sh`, `qtranslate-linux-x64`), the `release` job `needs: [windows, linux]`, the trixie artifacts and `--target astra`; only trixie artifacts are CI-published. |
| `doc/architecture.md:1238-1241`, `:1244` (§4.14) | "20 of 20 tests", "Linux platform actions are still stubs; they arrive ... tasks 02-04", table columns "Linux X11 (plan)"/"Linux Wayland (plan)". | The backends are implemented and the gate is 28/28. | 28 tests, implemented X11/Wayland backends, X11 full and Wayland limited; drop "(plan)" from the table header. |
| `doc/architecture.md:1272-1275` (§4.14) | "Planned artifacts", "CI — `ubuntu-24.04`, Qt 6.5+", "the other 20 tests are shared". | Artifacts exist and are checked; CI runs in `debian:trixie` on an `ubuntu-latest` runner; Linux has 28 tests. | Actual artifacts (`tar.gz`, `.deb`, AppImage, native Astra `-astra.tar.gz`/`+astra1`), the `debian:trixie` container, Qt 6.5+ minimum satisfied by 6.8.2; 20 shared + 8 platform tests; a real tag run is still not performed. |
| `doc/architecture.md:1483-1507`, `:1513-1517`, optionally `:1598` (§8/§8.4) | Test table and manual checklist list the 23 Windows tests only. | Missing the eight Linux tests and the X11 matrix. | Add the Linux tests and platform qualifiers, state 28 Linux/23 Windows, and add the Linux X11 matrix (or a pointer to this task); an §8.4 Linux record is the lead's choice. |
| `doc/architecture.md:1638` (§9.1) | "CI and releases: GitHub Actions on `windows-latest`". | The workflow now also has the Linux job and the release needs both. | GitHub Actions on a `windows-latest` Windows job and an `ubuntu-latest` runner with a `debian:trixie` Linux container job; a `v*` tag publishes both sets plus `SHA256SUMS.txt`. |
| `doc/architecture.md:1657`, `:1671` (§9.2) | "There is no Linux build in 0.11.0: platform modules are stubs, a full port is roadmap stage 2"; item 5 "Linux — roadmap stage 2". | True for 0.11.0 but it reads as the current state; the port is implemented for the next release. | Say that 0.11.0 has no Linux build, the Linux port is implemented on `main` for 0.12.0, and item 5 is done pending the release. |
| `doc/handoff.md:3` | "Date: 2026-11-07. Repository: `C:\projects\qtranslate`, branch `main`." | The current checkout is `/home/parsee/sync/mine/QTranslate` on Linux; the date is stale. | Update the date and the repository path; keep the branch `main`. |
| `doc/handoff.md:36-135` | "Linux baseline (2026-11-06, updated 2026-11-07)"; `package.sh` described only as the portable archive (`:86-89`); task 07 "children 01-02 are done and child 03 ... not started" (`:48`, `:132-134`); "The READMEs still say 20/20 tests (task 08)" (`:92-93`). | Packaging and CI are complete; task 08 is now done and the docs are for the lead; the not-performed list belongs here. | Update the current state (stage 2 gate, 28 tests, all six children done, CI wired but not run, 0.12.0 pending), describe `package.sh` with the `.deb`/AppImage/Astra targets, replace the task-07 progress sentence, point the README note at the lead's fix, and add the final not-performed/limitations list. |
| `doc/roadmap/qtranslate/plan.md:33` | "Old stage 11 (Linux support) is not started." | Stage 2 is at the final gate; only the lead's acceptance and the docs remain. | "Stage 2 is implemented and at the final verification gate; the release is pending." |
| `doc/roadmap/qtranslate/plan.md:26-27` | "After any other code changes ... all 23 tests pass." | On Linux the gate is 28 tests. | 23 Windows / 28 Linux (20 shared + 8 platform). |
| `doc/roadmap/qtranslate/02-linux/plan.md:15`, `:22` | The stage still lists "README and architecture updates" and CI publication as targets; `status: in-progress`. | No false pass is claimed; the stage `## Done when` is the lead's to accept, and the real GitHub run is not performed. | Keep the stage `in-progress`; the acceptance must record the missing README/architecture updates and the not-performed GitHub run/tag/release, not a pass. |
| `AGENTS.md:5-6` | "Windows tray translator ... The Linux port is planned; `src/platform/*_stub.cpp` are stubs". | The Linux port is implemented; stubs are only the non-Windows/non-Linux fallback. | QTranslate builds on Windows (MSVC) and Linux (GCC, CMake/Ninja); Linux has X11 hotkeys/capture/autostart and the limited Wayland path. |
| `AGENTS.md:22` | `build/cli-build.cmd` hardcodes `C:\projects\qtranslate`. | The current checkout is `/home/parsee/sync/mine/QTranslate`; the helper is Windows-only. | Say that the helper is Windows-only and stale for this checkout; use manual CMake on Linux. |
| `AGENTS.md:30` | "all 23 tests must pass". | Linux has 28 tests. | 23 Windows / 28 Linux (20 shared + 8 platform). |
| `AGENTS.md:33` | "`tst_hotkeymanager`, `tst_autostart` and `tst_secretprotector` are Windows-only." | `tst_autostart` and `tst_secretprotector` also build on Linux with platform implementations; `tst_hotkeymanager` is the Windows-only name. | `tst_hotkeymanager` is Windows-only; `tst_autostart`/`tst_secretprotector` have Windows and Linux variants; the Linux-only tests are the X11/Wayland/CLI ones. |
| `AGENTS.md:59-63` | CI/release described as Windows-only. | The workflow has the `linux` job and the release needs both. | Add the `debian:trixie` Linux job and `qtranslate-linux-x64`; keep the Windows Qt 6.8.3 details. |
| `AGENTS.md:96` | "API keys and proxy passwords use DPAPI on Windows." | The Linux path uses libsecret or the 0600 fallback. | Windows: DPAPI. Linux: libsecret, or a 0600 file with a warning when the keyring is unavailable. |

Also checked and accurate: `README.md`/`README.ru.md` Astra section; `doc/architecture.md:38-58`
platform rows; `doc/code-signing.md` (no Linux change); the stage `## Done when` (no false pass
claim). `AGENTS.md` out-of-scope items (the `C:\projects` helper, the Windows-only test names) are
reported because the lead decides.

### Not performed

- **Windows Debug/Release and the 23 Windows tests**: no Windows/MSVC in this environment. The
  workflow and the Windows sources were reviewed by diff; nothing is claimed as passed.
- **A real `GlobalShortcuts` portal desktop**: Astra runs X11 and Weston has no portal. The
  automated gate is `tst_hotkeymanager_wayland` 20/20 (mock D-Bus).
- **Live Wayland clipboard/in-place with `wl-copy`/`wl-paste`**: `wl-clipboard` was installed and
  removed; the nested Weston seat delivered no selection offer (`wayland/wl-popup-log.txt`:
  `Read 0 characters from the clipboard`). The unit coverage is
  `tst_selectiongrabber_linux` 12/12 and `tst_windowactivation_linux` 13/13. The visible
  `Limited Wayland support` dialog was captured.
- **The trixie libsecret round-trip**: `tst_secretprotector` is 7 passed + 1 skipped in the
  trixie image (no session keyring); covered on Astra, 8/8.
- **The real GitHub Actions run, `v*` tag push, and release publication**: they need the user's
  explicit approval; nothing was pushed. `git log --oneline origin/main..HEAD` shows the seven
  local commits, and `git tag --list 'v0.12*'` is empty.
- **`actions/checkout@v7` safe-directory behavior outside GitHub**: only the static YAML was
  checked; the Windows job block is unchanged byte-for-byte.

### Hygiene and resulting state

- `git status --short` shows only this plan and the journal; `git diff --check` is clean; the diff
  contains no source, test, CMake, packaging, or documentation file.
- No `qtranslate` process, no installed package, no Weston/Xvfb/kate process, no root-owned file
  in the repository; `wl-clipboard` is removed; the user's clipboard was restored (md5
  `0d111f56964985f562aaaaf9d2980787`, matching `x11/user-clipboard-original.txt`).
- The application logs contain no translated text, API keys, or passwords. The only `Hallo Welt`
  occurrences in the evidence are the deliberate matrix files (`x11/kate-target.txt`,
  `x11/ctrlshiftq-file.txt`); the test logs mention the literal setting name `apiKey` in test
  names/messages, not a value.
- This task sets `status: done` and commits this plan and the journal. The stage `02-linux` stays
  `in-progress` for the lead: the documentation gaps above, the stage acceptance, and the 0.12.0
  decision remain. The step 8 command correction is described above.

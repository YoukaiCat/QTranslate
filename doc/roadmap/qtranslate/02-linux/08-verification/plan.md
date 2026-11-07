---
kind: task
status: todo
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

## Done when

- Trixie container: Debug and Release configure and build with Qt 6.8.2 and `-Wall -Wextra`, no
  warnings in the logs; `xvfb-run -a ctest --output-on-failure` passes every test (the 20 shared
  tests plus the Linux platform tests). The application starts under `xvfb-run` (`--version`).
- Astra Linux: native Debug/Release build with Qt 6.10.2 and `ctest` pass; the application launches
  under the real X11 session. The **native Astra `.deb`** (from `05-astra-artifacts`) installs with
  `dpkg -i`, `qtranslate --version` and the GUI start, the desktop entry/icon/autostart/settings/log
  paths are checked, and `dpkg -r` removes it. The trixie `.deb` is checked to fail with the exact
  unsatisfied dependencies and the trixie `tar.gz`/AppImage to fail on glibc; neither is claimed on
  Astra. If `sudo` needs the user, the exact commands and the reason are recorded as not performed.
- X11 scenario matrix on the real `:0` session (or a scripted Xvfb check plus the real session where
  possible): Ctrl+Q shows the popup with the selection, Ctrl+Shift+Q replaces it, Ctrl+W fills the
  main window; the tray menu (show, pause hotkeys, service, settings, quit) works; settings open and
  save; autostart toggles the XDG file; `--translate-popup`, `--translate-main`, and
  `--translate-inplace` work with and without a running instance; the single-instance activation and
  command forwarding work; the clipboard is restored. Record the observations.
- Wayland: Weston headless (`weston --backend=headless-backend.so`, `QT_QPA_PLATFORM=wayland`)
  verifies the limited path: the app starts, the missing portal is reported, no X11 display or
  synthetic input is used, the selection is read from the clipboard the user copied, in-place copies
  the translation to the clipboard, and the warnings are visible. The `GlobalShortcuts` portal happy
  path is verified only with a mock DBus service or a portal-capable desktop; otherwise it is
  recorded as not performed with the reason. No `ydotool`/`wtype` or equivalent is present or
  promised.
- Artifacts: the trixie `tar.gz`, `.deb`, and AppImage are checked on trixie (the AppImage with
  `--appimage-extract-and-run`); the native Astra `tar.gz` runs on Astra with no Qt in
  `LD_LIBRARY_PATH`, and the native Astra `.deb` installs and launches there. The trixie artifacts
  are not claimed on Astra: the exact dependency/glibc failure is recorded. The Astra artifacts are
  local verification artifacts; only the trixie artifacts are published by CI (release artifacts
  stay CI-built). The package contents and `Depends` show trixie dependencies; no Ubuntu target
  remains.
- CI: the workflow has a Linux job inside a `debian:trixie` container (or a self-hosted Debian/Astra
  runner), runs `xvfb-run -a ctest`, builds the Linux artifacts, and the release job includes both
  Windows and Linux artifacts. The actual GitHub run and tag publication require the user's push
  approval and are recorded as not performed until then.
- Documentation: `README.md`, `README.ru.md`, and `doc/architecture.md` §4.14 are checked for the
  Linux installation, Debian 13 (trixie) and Astra support, the Qt 6.5+ minimum (trixie ships 6.8.2),
  X11/Wayland limitations, the tray/GNOME note, and the artifact names. Gaps are reported to the
  lead; this task does not edit those files.
- Hygiene: `git diff --check` is clean; only the stage's intended files changed; logs contain no
  translated text, API keys, or passwords; the outcome lists every not-performed check with the
  reason and never claims a Windows or GitHub result that did not run.

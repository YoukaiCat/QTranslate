---
kind: stage
status: blocked
base: 584e722
---

# Linux support (old stage 11)

## Goal

Build QTranslate for Linux x86_64 with the same scenarios as Windows where the platform allows: tray, main window, Ctrl+Q/Ctrl+Shift+Q/Ctrl+W, settings and autostart, portable archive, and packages. X11 is the full target; Wayland is limited and must show clear warnings.

## What to do, what is touched

Linux implementations of `platform/*` (XDG autostart, X11 `XGrabKey`, the Wayland `GlobalShortcuts` portal where the desktop provides it, X11 XTest and clipboard selection capture, active-window activation, libsecret with a 0600-file fallback); CLI commands `--translate-popup`, `--translate-main`, `--translate-inplace`; `.desktop`, hicolor icons, `share/applications`; `tar.gz`, `.deb`, and AppImage built for Debian 13 (trixie) in a `debian:trixie` container, with installation and launch checked on Astra Linux using native Astra artifacts; a `debian:trixie` container job in CI with `xvfb-run`; README and architecture updates for installation and platform limitations. The trixie `tar.gz` and AppImage bundle Qt but inherit trixie's glibc baseline, so they are not claimed on Astra. The old plan's stage 11 lists the detailed scope.

## Done when

- The project builds and all tests pass on Linux without warnings.
- X11 supports all three translation scenarios and autostart.
- Wayland supports the available scenarios and states its limitations to the user.
- `tar.gz`, `.deb`, and AppImage are built and published by CI on a tag.
- README and architecture describe installation and limitations.

## Depends on

None technically; follows stage 1 in the old order. The primary distro is Debian 13 (trixie) with Debian 13 dependencies; trixie ships Qt 6.8.2, so the project's Qt 6.5+ minimum stays unchanged. Astra Linux (Qt 6.10.2) is mandatory for build, launch, and `.deb` checks; the trixie `.deb` cannot satisfy Astra's dependencies (measured: `libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), and the trixie `tar.gz`/AppImage cannot run there because the bundled Qt needs `GLIBC_2.38` while Astra has 2.36, so a separate native Astra build covers Astra and support must not be dropped. X11 is the full target; Wayland is limited to the `GlobalShortcuts` portal where available, manual copying, CLI commands, and explicit warnings.

## Risks and what is needed from the user

Wayland does not guarantee global shortcuts or synthetic input; X11 is the reliable target and Wayland relies on the `GlobalShortcuts` portal where available, manual copying, and CLI commands. No `ydotool`/`wtype` or other synthetic input is added under Wayland. The user must provide the Debian 13 (trixie) container/CI access and is checking the `.deb`, portable archive, and launch on Astra Linux. If the environment is unavailable, the lead may mark the stage blocked and ask whether to start an independent stage earlier.

## Open question

The acceptance passed on 2026-11-08 with the fix `da3fefc`: Astra and trixie Debug/Release are 28/28
with no warnings, the integrated diff and the documentation were reviewed, and the only `## Done
when` item left is the CI publication of the artifacts on a tag. That is the 0.12.0 release and
needs the user's approval (version bump, release notes, push `main`, tag). The stage is `blocked`
until that decision.

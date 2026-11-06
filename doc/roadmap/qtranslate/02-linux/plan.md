---
kind: stage
status: in-progress
base: 584e722
---

# Linux support (old stage 11)

## Goal

Build QTranslate for Linux x86_64 with the same scenarios as Windows where the platform allows: tray, main window, Ctrl+Q/Ctrl+Shift+Q/Ctrl+W, settings and autostart, portable archive, and packages. X11 is the full target; Wayland is limited and must show clear warnings.

## What to do, what is touched

Linux implementations of `platform/*` (XDG autostart, X11 `XGrabKey`, the Wayland `GlobalShortcuts` portal where the desktop provides it, X11 XTest and clipboard selection capture, active-window activation, libsecret with a 0600-file fallback); CLI commands `--translate-popup`, `--translate-main`, `--translate-inplace`; `.desktop`, hicolor icons, `share/applications`; `tar.gz`, `.deb`, and AppImage built for Debian 13 (trixie) in a `debian:trixie` container (`tar.gz` and AppImage are distro-independent), with installation and launch checked on Astra Linux; a `debian:trixie` container job in CI with `xvfb-run`; README and architecture updates for installation and platform limitations. The old plan's stage 11 lists the detailed scope.

## Done when

- The project builds and all tests pass on Linux without warnings.
- X11 supports all three translation scenarios and autostart.
- Wayland supports the available scenarios and states its limitations to the user.
- `tar.gz`, `.deb`, and AppImage are built and published by CI on a tag.
- README and architecture describe installation and limitations.

## Depends on

None technically; follows stage 1 in the old order. The primary distro is Debian 13 (trixie) with Debian 13 dependencies; trixie ships Qt 6.8.2, so the project's Qt 6.5+ minimum stays unchanged. Astra Linux (Qt 6.10.2) is mandatory for build, launch, and `.deb` checks; if a trixie `.deb` cannot satisfy Astra's dependencies, the portable `tar.gz`/AppImage or a separate Astra build covers it, and Astra support must not be dropped. X11 is the full target; Wayland is limited to the `GlobalShortcuts` portal where available, manual copying, CLI commands, and explicit warnings.

## Risks and what is needed from the user

Wayland does not guarantee global shortcuts or synthetic input; X11 is the reliable target and Wayland relies on the `GlobalShortcuts` portal where available, manual copying, and CLI commands. No `ydotool`/`wtype` or other synthetic input is added under Wayland. The user must provide the Debian 13 (trixie) container/CI access and is checking the `.deb`, portable archive, and launch on Astra Linux. If the environment is unavailable, the lead may mark the stage blocked and ask whether to start an independent stage earlier.

---
kind: stage
status: todo
---

# Linux support (old stage 11)

## Goal

Build QTranslate for Linux x86_64 with the same scenarios as Windows where the platform allows: tray, main window, Ctrl+Q/Ctrl+Shift+Q/Ctrl+W, settings and autostart, portable archive, and packages. X11 is the full target; Wayland is limited and must show clear warnings.

## What to do, what is touched

Linux implementations of `platform/*` (XDG autostart, X11 `XGrabKey`, X11 XTest and clipboard selection capture, active-window activation, libsecret with a 0600-file fallback); CLI commands `--translate-popup`, `--translate-main`, `--translate-inplace`; `.desktop`, hicolor icons, `share/applications`; `tar.gz`, `.deb`, and AppImage; `ubuntu-24.04` CI with `xvfb-run`; README and architecture updates for installation and platform limitations. The old plan's stage 11 lists the detailed scope.

## Done when

- The project builds and all tests pass on Linux without warnings.
- X11 supports all three translation scenarios and autostart.
- Wayland supports the available scenarios and states its limitations to the user.
- `tar.gz`, `.deb`, and AppImage are built and published by CI on a tag.
- README and architecture describe installation and limitations.

## Depends on

None technically; follows stage 1 in the old order. It needs a Linux machine or Linux CI and a decision on supported desktops.

## Risks and what is needed from the user

Wayland does not guarantee global shortcuts or synthetic input; X11 is the reliable target and Wayland needs a portal or CLI fallback. The user must provide a Linux environment or CI access, choose the supported desktops, and accept the X11-first/Wayland-limited scope. If the environment is unavailable, the lead may mark the stage blocked and ask whether to start an independent stage earlier.

---
kind: stage
status: done
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

## Outcome

Accepted on 2026-11-08 over `584e722..da3fefc` (the acceptance fix is `da3fefc`; the documentation
corrections are `bc86eff`). All eight children are `done`.

- **Build and tests:** Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2) and `qtranslate-trixie` (Qt 6.8.2,
  GCC 14.2) build Debug and Release with no warnings and pass 28/28; Clang 16 is clean. Per-binary
  counts are in `08-verification`; `tst_secretprotector` is 7+1 in trixie (no session keyring) and
  8/8 on Astra.
- **X11:** the real `:0` matrix passed in `08-verification` (Ctrl+Q popup, Ctrl+Shift+Q in-place,
  Ctrl+W main window, tray, settings, autostart, CLI with and without an instance, single-instance
  forwarding, clipboard restore); the X11 code did not change after it. The grab-release fix
  (`5b9f9fa`) and the Astra trusted-client setup are part of the stage.
- **Wayland:** the `GlobalShortcuts` portal backend (mock DBus 20/20), the limited fallback with a
  visible warning, no X11 connection under Weston, and no synthetic input. A real portal-capable
  desktop and live Wayland clipboard/in-place were not available and are not performed.
- **Artifacts:** the trixie `tar.gz`, `.deb`, and AppImage and the native Astra `-astra.tar.gz` and
  `+astra1_amd64.deb` are built by `package.sh`; hashes, install/remove, `ldd`, plugin and Qt
  translation checks are in `07-packaging-and-ci` and `08-verification`. The CI and release jobs
  are verified statically; the actual GitHub run, tag, and publication were **not performed**: the
  user deferred the 0.12.0 release on 2026-11-08, so this `## Done when` item is recorded as not
  performed, not as passed.
- **Documentation:** both READMEs, `AGENTS.md`, `doc/architecture.md`, and `doc/handoff.md` describe
  the Linux install, packages, CLI, XDG paths, tests, and limitations; the acceptance corrected the
  four factual errors found by the review.

One real defect was found and fixed during acceptance (`da3fefc`): an automatic save at exit deleted
an unreadable secret token from the INI when the keyring was unavailable at load; the regression
test `failedSecretLoadKeepsStoredToken` covers it. The `write_checksums` comment was corrected in
the same commit.

Real problems left unfixed because of their price: old fallback secret files and keyring entries
are never deleted when a secret is overwritten or migrated (no `SecretProtector::forget`), so an
obsolete plaintext `0600` file can remain; a build without libsecret drops an `l:` token from the
INI by design; the AppImage `ldd` gate checks only `not found`, plugin presence is per directory,
the tags are moving, and artifacts are not byte-reproducible; the tray tooltip can show the
configured sequence before an asynchronous portal bind; the `secretprotector.h` comment "keeps `l:`
tokens unconditionally" is inaccurate for a no-libsecret build. Windows Debug/Release and the 23
Windows tests were not performed (no MSVC), and a real Wayland portal was not available.

---
kind: stage
status: in-progress
base: 7db5e60
depends: [01-linux-build-baseline, 02-global-hotkeys, 03-x11-selection-and-inplace, 04-x11-hotkey-grab-release, 05-xdg-autostart-and-secrets, 06-cli-and-desktop-integration]
---

# Debian 13 (trixie) packages and CI

## Goal

Package QTranslate for Linux and publish the trixie artifacts from CI. Debian 13 (trixie, Qt 6.8.2)
is the primary `.deb` and CI target: the `.deb`, the portable `tar.gz`, and the AppImage are built in a
`debian:trixie` container against trixie's Qt and dependencies. Astra Linux 1.8.7 (Qt 6.10.2) is
mandatory for build, launch, and `.deb` checks, but the trixie artifacts cannot serve it: the trixie
`.deb` `Depends` from `dpkg-shlibdeps` names `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)`, while
Astra ships `libglib2.0-0` 2.74 and `libxtst6` 2:1.2.3; the trixie `tar.gz`/AppImage bundle Qt 6.8.2,
whose `libQt6Gui`/`libQt6Network` require `GLIBC_2.38`, while Astra has glibc 2.36. A separate
native Astra build therefore produces the Astra `tar.gz` and `.deb`; the trixie `.deb` is not claimed
to install on Astra. Astra support must not be dropped.

`tar.gz` and AppImage are portable in the sense that they bundle Qt and need no Qt installed, but
they inherit the trixie glibc 2.41 baseline: they run on trixie and later, not on Astra. Ubuntu is
not a build or package target. The Qt 6.5+ minimum stays unchanged; do not lower it to 6.4.
AppImage stays best-effort but is built and uploaded by CI; Flatpak and Snap are not added.

The GitHub-hosted runner may stay Ubuntu, but the Linux job must run inside a `debian:trixie`
container (or a self-hosted Debian/Astra runner if the container does not fit). The `.desktop` file
and hicolor icons are installed by CMake so the `.deb` and the portable archives share one layout.
The Linux packaging script, not Qt's deploy script, owns the Linux artifact layout: the generic
deploy tool on Linux copies no Qt libraries and writes a distro-specific `qt.conf`, so the Windows
deploy block is guarded with `if(WIN32)` in the first child.

## What to do, what is touched

The work is split into six children. Each one has its own result and gate; later children extend
the same `packaging/linux/package.sh` and the same CMake install layout.

1. [01-desktop-entry-and-install](01-desktop-entry-and-install/plan.md) — the `.desktop` file,
   the 256x256 hicolor icon from `resources/icons/app.png`, CMake install into
   `share/applications` and `share/icons/hicolor`, the `desktop-file-validate` gate, and the
   `if(WIN32)` guard for the deploy block so the Linux install tree contains only what the
   packaging script owns.
2. [02-portable-tarball](02-portable-tarball/plan.md) — `packaging/linux/package.sh` with
   `--build-dir`, `--output-dir`, `--version`; a `debian:trixie`/trixie-host check; the
   `qtranslate-<version>-linux-x86_64.tar.gz` with bundled Qt 6.8.2 libraries, plugins, Qt
   translations, `qt.conf`, RPATH, the desktop file, and the icon; `SHA256SUMS-linux.txt`.
3. [03-deb-package](03-deb-package/plan.md) — the same script builds
   `qtranslate_<version>_amd64.deb` with `usr/bin/qtranslate`, the desktop file, the icon, and a
   control file whose `Depends` comes from `dpkg-shlibdeps` against trixie; `dpkg-deb --info`,
   install/remove on trixie; `libsecret-1-0` appears only when the build links libsecret.
4. [04-appimage](04-appimage/plan.md) — the AppImage with bundled Qt, built with pinned
   `linuxdeploy`, `linuxdeploy-plugin-qt`, and `appimagetool` downloads, run with
   `--appimage-extract-and-run` when FUSE is unavailable; checksum entry.
5. [05-astra-artifacts](05-astra-artifacts/plan.md) — the native Astra build of the same script:
   `qtranslate-<version>-linux-x86_64-astra.tar.gz` and
   `qtranslate_<version>+astra1_amd64.deb`, installed and launched on Astra, with the trixie
   artifacts' exact dependency/glibc failure recorded.
6. [06-ci-linux-job](06-ci-linux-job/plan.md) — the `debian:trixie` container job in
   `.github/workflows/build.yml` (build, `timeout 300 xvfb-run -a ctest`, `package.sh`, upload
   `qtranslate-linux-x64`) and the release job that downloads both Windows and Linux artifacts;
   the existing tag/version check stays.

## Done when

- `packaging/linux/qtranslate.desktop` exists, passes `desktop-file-validate`, and CMake installs
  it to `<prefix>/share/applications` together with
  `<prefix>/share/icons/hicolor/256x256/apps/qtranslate.png`; the Linux install tree has no
  `bin/qt.conf` or `share/qt6/translations` from the Windows deploy script, and the installed
  binary keeps `RUNPATH $ORIGIN:$ORIGIN/../lib`.
- `packaging/linux/package.sh` accepts `--build-dir`, `--output-dir`, `--version`, builds on
  trixie or in the `debian:trixie` image, and produces
  `qtranslate-<version>-linux-x86_64.tar.gz`, `qtranslate_<version>_amd64.deb`,
  `qtranslate-<version>-x86_64.AppImage`, and `SHA256SUMS-linux.txt`. The tar.gz contains
  `bin/qtranslate`, bundled Qt 6.8.2 libraries and plugins, the Qt translations, `bin/qt.conf` with
  `Prefix = ..`, `Plugins = plugins`, `Translations = translations`, the desktop file, and the
  icon; it runs with `LD_LIBRARY_PATH` unset and `ldd` resolves Qt to the bundle. The `.deb`
  installs and removes on trixie, its `Depends` comes from `dpkg-shlibdeps` (including
  `libsecret-1-0` when the build links libsecret and `libqt6dbus6` for the portal), and
  `dpkg-deb --info` is recorded. The AppImage runs with `--appimage-extract-and-run`; its tool
  versions and URLs are pinned in the script.
- The Astra facts are recorded with evidence: the trixie `.deb` cannot install on Astra because of
  the exact unsatisfied dependencies (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), and the trixie
  `tar.gz`/AppImage cannot run there because the bundled Qt 6.8.2 libraries require
  `GLIBC_2.38` (Astra has 2.36); neither is ever reported as working on Astra. The native Astra
  build produces `qtranslate-<version>-linux-x86_64-astra.tar.gz` and
  `qtranslate_<version>+astra1_amd64.deb`; the application builds and launches on Astra,
  `qtranslate --version` and the GUI under the X11 session are checked, `dpkg -i` installs and
  `dpkg -r` removes the Astra `.deb`, the desktop entry, icon, autostart file, settings, and log
  paths are checked from the installed package, and `SHA256SUMS-linux-astra.txt` is written. If
  `sudo` is unavailable, the exact commands are left for the user and the check is recorded as not
  performed, never as passed.
- `.github/workflows/build.yml` has the Linux job on `debian:trixie` that installs the trixie build
  dependencies, configures Release, builds, runs
  `timeout 300 xvfb-run -a ctest --output-on-failure`, runs `packaging/linux/package.sh`, and
  uploads `qtranslate-linux-x64`; the `release` job needs both jobs, downloads both artifacts into
  one directory, and publishes them on a `v*` tag; the existing tag/version check still gates the
  release. The actual GitHub run and tag publication require the user's push approval and are
  recorded as not performed until then.
- No step builds or packages on Ubuntu; no Ubuntu-only dependency or PPA is used; Debian 12 is not
  a target. No Flatpak/Snap is added. The documentation is the lead's: the report carries the
  install commands, the trixie and Astra dependency facts, the artifact names, and the
  not-performed checks.

## Depends on

Tasks `01-linux-build-baseline` through `06-cli-and-desktop-integration` are done; the Linux tree
builds and CTest is 28/28 on trixie and Astra, and the three CLI commands and the Linux platform
behaviour are in place. The packaging work does not change runtime behaviour; it only adds the
install layout, the scripts, and CI.

## Risks and what is needed from the user

- **Astra is not reachable with the trixie artifacts.** Measured facts: Astra Linux 1.8.7 has glibc
  2.36 and `libglib2.0-0` 2.74.6, `libxtst6` 2:1.2.3, and Qt 6.10.2 under t64 names. Trixie's
  `dpkg-shlibdeps` produces `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)` for the packaged binary,
  so `dpkg -i` cannot satisfy it; the bundled trixie Qt 6.8.2 `libQt6Gui`/`libQt6Network` reference
  `GLIBC_2.38`, so the portable archive/AppImage cannot load on Astra. The trixie-built executable
  itself only needs `GLIBC_2.34` and `GLIBCXX_3.4.14`, so the blocker is the package names/versions
  and the bundled Qt, not the application code. The only correct Astra path is the native Astra
  build; never claim a trixie `.deb`/tar.gz/AppImage works on Astra, and do not lower Qt or build on
  Debian 12 (Qt 6.4 is below the 6.5 minimum).
- **Qt's Linux deploy script is not a packager.** `qt_generate_deploy_app_script` on Linux uses the
  generic deploy tool: it logs `DEPLOY_TOOL_OPTIONS has no effect`, copies no Qt libraries (system
  lib dirs are excluded), and writes a `qt.conf` with the trixie
  `lib/x86_64-linux-gnu/qt6/plugins` path. The guard in child 01 and the script-owned layout in
  child 02 are mandatory; do not rely on `cmake --install` to produce a portable tree.
- **Transitive library resolution.** Qt 6.8.2 libraries have no RUNPATH; the executable's
  `$ORIGIN/../lib` is not transitive. Every bundled non-system library (Qt, ICU, pcre2,
  double-conversion, b2, zstd, and Wayland if included) must get
  `patchelf --set-rpath '$ORIGIN'`, and the bundle must exclude glibc, libGL/EGL/GLX driver
  libraries, and other system-critical libraries. Verify with `ldd` and `QT_DEBUG_PLUGINS=1`, not
  only by starting the app on the build machine.
- **libsecret.** The trixie build links `libsecret-1`, so the `.deb` `Depends` must contain
  `libsecret-1-0` (from `dpkg-shlibdeps`, not by hand). A build without libsecret must stay
  buildable and must not gain that dependency. The portable archive/AppImage either need
  `libsecret-1-0`/glib on the target or must be built without libsecret; record which choice was
  made, and never add a manual `Depends` for a library the binary does not link.
- **`.deb` dependencies and reproducibility.** `dpkg-shlibdeps` needs a `debian/control` source
  stanza, `dpkg-dev`, and the runtime packages; run it on the staged binary, not on the build-tree
  binary. Pin the AppImage tool versions and SHA256; do not use `continuous` builds. A
  network-less local run may skip the AppImage, but CI must fail rather than silently omit it.
- **CI container.** GitHub Actions container jobs run as root; `debian:trixie` lacks `git` and
  `ca-certificates` before checkout. Install them first, then the build/packaging dependencies.
  `xvfb-run` needs `xauth`; the workflow wraps it in `timeout 300` because a hung run happened
  before. The release job must keep `SHA256SUMS-linux.txt` distinct from the Windows
  `SHA256SUMS.txt`, because both are downloaded into one directory.
- **Astra install checks need privileges.** `dpkg -i`/`dpkg -r` and the trusted-client X11 setup use
  `sudo` on the Astra machine; if it is unavailable, record the exact commands and the reason as
  not performed. Rebuilds require `sudo /usr/bin/fly-set-execid.sh` before the X11 tests; a fresh
  build tree otherwise QSKIPs two X11 tests.
- **Scope.** This stage is broad. If a child outgrows its plan, stop and report instead of taking a
  shortcut. Publishing (push, tag, release assets) is an external action and needs the user's
  explicit approval; this stage does not push or tag.
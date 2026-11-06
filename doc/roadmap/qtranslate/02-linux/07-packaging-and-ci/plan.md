---
kind: task
status: todo
depends: [01-linux-build-baseline, 02-global-hotkeys, 03-x11-selection-and-inplace, 04-x11-hotkey-grab-release, 05-xdg-autostart-and-secrets, 06-cli-and-desktop-integration]
---

# Debian 13 (trixie) packages and CI

## Task

Package QTranslate for Linux and publish the artifacts from CI. The primary target is Debian 13
(trixie): the `.deb` is built in a `debian:trixie` container against trixie's Qt 6.8.2 and its
dependencies come from trixie. Astra Linux is mandatory: the application must build and launch
there (Qt 6.10.2), and the `.deb` must install there when its dependencies allow. If a trixie
`.deb` cannot satisfy Astra's package names or versions, cover Astra with the portable `tar.gz` or
AppImage, or with a separate Astra build; Astra support must not be dropped.

`tar.gz` (portable, bundled Qt) and AppImage are distro-independent and are built in the same
trixie container. The GitHub-hosted runner may stay Ubuntu, but the Linux job must run inside a
`debian:trixie` container (or on a self-hosted Debian/Astra runner if the container does not fit).
Do not use Ubuntu as a build or package target. The Qt 6.5+ minimum stays unchanged; do not lower
it to 6.4.

The `.desktop` file and hicolor icons are installed by CMake so the `.deb` and the portable archive
share one layout. The Linux packaging script, not Qt's deploy script, owns the Linux artifact
layout; if the current Windows-specific deploy block interferes with the Linux configure or
install, guard it with `if(WIN32)` and report it.

## Done when

- `packaging/linux/qtranslate.desktop` exists with `Type=Application`, `Name=QTranslate`,
  `Exec=qtranslate`, `Icon=qtranslate`, `Terminal=false`, a suitable `Categories`/`Keywords`, and
  passes `desktop-file-validate`. CMake installs it to `share/applications` and installs a hicolor
  icon (at least `256x256/apps/qtranslate.png`, generated from `resources/icons/app.png`; smaller
  sizes are optional) to `share/icons/hicolor`.
- `packaging/linux/package.sh` accepts `--build-dir`, `--output-dir`, and an optional `--version`,
  and builds in a `debian:trixie` container or a trixie host:
  - `qtranslate-<version>-linux-x86_64.tar.gz`: `bin/qtranslate`, bundled Qt 6.8.2 libraries and
    plugins, Qt translations at `bin/../translations`, RPATH `$ORIGIN/../lib`, the desktop file and
    icon under `share/`. It runs on Astra with no Qt in `LD_LIBRARY_PATH`.
  - `qtranslate_<version>_amd64.deb`: `usr/bin/qtranslate`, the desktop file and icon, and a control
    file whose `Depends` comes from `dpkg-shlibdeps` against trixie (`dpkg-dev` is installed in the
    packaging image). The package installs on trixie and, if the dependencies allow, on Astra Linux
    (Qt 6.10.2); `dpkg-deb --info` is recorded. If Astra cannot install the trixie package, the
    outcome names the exact unsatisfied dependency and the Astra fallback used (tar.gz, AppImage, or
    a separate Astra build), and Astra is still launched.
  - an AppImage with bundled Qt, built with `linuxdeploy`/`linuxdeploy-plugin-qt`/`appimagetool`
    downloaded into the build tools directory (trixie does not package them); use
    `--appimage-extract-and-run` when FUSE is unavailable.
  - `SHA256SUMS-linux.txt` with the Linux artifacts; do not reuse the Windows `SHA256SUMS.txt` name,
    because the release job downloads both artifacts into one directory.
- Astra Linux: the application builds and starts from source (if the machine has the toolchain) or
  from the portable archive; `qtranslate --version` and the GUI under the X11 session are checked;
  the `.deb` is installed with `dpkg -i` and removed with `dpkg -r` where dependencies allow. The
  desktop entry, icon, autostart file, and settings/log paths are checked from the installed package.
  If `sudo` is unavailable, the exact commands are left for the user and the check is recorded as
  not performed, never as passed.
- CI: `.github/workflows/build.yml` has a Linux job on a `debian:trixie` container (the host runner
  may be `ubuntu-latest`; install `git`/`ca-certificates` before checkout if the image lacks them).
  The job installs the trixie build dependencies, configures Release, builds, runs
  `xvfb-run -a ctest --output-on-failure`, runs `packaging/linux/package.sh`, and uploads
  `qtranslate-linux-x64`. The `release` job downloads both Windows and Linux artifacts and publishes
  them on a `v*` tag; the existing tag/version check still applies. The actual GitHub run and tag
  publication require the user's push approval and are recorded as not performed until then.
- No task step builds or packages on Ubuntu; no Ubuntu-only dependency or PPA is used. Debian 12 is
  not a target; it may only be mentioned as optional compatibility. The documentation is the lead's:
  report the install commands, dependencies, trixie/Astra facts, and artifact names instead of
  editing `README.md`/`README.ru.md`/`doc/architecture.md`.

## Risks

- Trixie's Qt is 6.8.2 and the project requires 6.5+; do not lower the requirement and do not build
  against a Qt installer or an Ubuntu package. The `.deb` dependencies must come from trixie.
- Astra's Qt is 6.10.2, which satisfies a trixie-built binary, but package names, versions, and
  `libsecret`/`libxcb` dependencies may differ. Test `dpkg -i` early; if it fails, record the exact
  dependency and use the portable archive/AppImage/separate Astra build rather than dropping Astra.
- `dpkg-shlibdeps` needs the packaging image to have `dpkg-dev` and the runtime packages; run it on
  the staged binary, not on the build tree binary.
- The portable archive must find Qt plugins and its own translations; a wrong `qt.conf`/RPATH makes
  it fail only on a clean machine. Test with `LD_LIBRARY_PATH` unset and a temporary `HOME`.
- The AppImage tooling is downloaded, not packaged; pin a version/URL or record the download in the
  script so the build is reproducible. If the network is unavailable locally, the CI job builds it.
- The GitHub Actions container job runs as root and the `debian:trixie` image is minimal; install
  `git`/`ca-certificates` before `actions/checkout` if needed.
- Publishing a tag is an external action and needs explicit user approval; do not push or tag from
  this task.

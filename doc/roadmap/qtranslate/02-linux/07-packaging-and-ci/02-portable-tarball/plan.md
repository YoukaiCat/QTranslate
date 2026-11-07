---
kind: task
status: todo
depends: [01-desktop-entry-and-install]
---

# Portable tar.gz with bundled Qt

## Task

Add `packaging/linux/package.sh` and make its first artifact the portable archive
`qtranslate-<version>-linux-x86_64.tar.gz`. The script is the only owner of the Linux bundle
layout; the CMake deploy block no longer runs on Linux after child 01.

Contract:

- `packaging/linux/package.sh` is a Bash script with `set -euo pipefail` and accepts
  `--build-dir` (default `build/linux-release`), `--output-dir` (default `build/artifacts-linux`),
  an optional `--version` (default read from `project(qtranslate VERSION ...)` in
  `CMakeLists.txt`), and `--help`. Unknown arguments and a missing `CMakeCache.txt` in the build
  directory exit non-zero with a clear message. `--version` must be `X.Y.Z`; it is used only for
  artifact names here.
- The script must run on a Debian 13 (trixie) host or inside the `debian:trixie` image built from
  `packaging/linux/trixie.Dockerfile`. On another host (including Astra) it prints the exact
  `docker run --rm -v "$PWD:/src" -w /src qtranslate-trixie ./packaging/linux/package.sh ...`
  command and exits 1; the Astra mode is added in child 05. Do not build the "trixie" artifacts on
  Astra: their glibc baseline must be trixie's.
- `cmake --install "$build_dir" --prefix "$stage"` produces `bin/qtranslate` plus the desktop file
  and icon from child 01. The script assembles this root:

  ```
  qtranslate-<version>-linux-x86_64/
    bin/qtranslate
    bin/qt.conf
    lib/                 Qt 6.8.2 libraries and their private dependencies
    plugins/             platforms/, tls/, imageformats/ (and wayland if bundled)
    translations/        Qt's qt_<lang>.qm files
    share/applications/qtranslate.desktop
    share/icons/hicolor/256x256/apps/qtranslate.png
  ```

- Qt sources come from the same Qt that built the binary: use `qtpaths6 --query QT_INSTALL_LIBS`,
  `QT_INSTALL_PLUGINS`, and `QT_INSTALL_TRANSLATIONS` (Qt 6.8.2 in trixie), or the build's
  `Qt6Core_DIR`. Copy `libQt6*.so.6*` (including `libQt6XcbQpa`) with symlinks preserved, plus the
  needed non-Qt private dependencies: at least `libicui18n.so.76`, `libicuuc.so.76`,
  `libicudata.so.76`, `libdouble-conversion.so.3`, `libb2.so.1`, `libpcre2-16.so.0`,
  `libpcre2-8.so.0`, and `libzstd.so.1`. Do not bundle glibc, libGL/libEGL/libGLX or other
  driver-critical libraries.
- Plugins: copy `platforms/libqxcb.so`, `platforms/libqoffscreen.so`, `platforms/libqminimal.so`,
  all `tls/` plugins (`libqopensslbackend.so`, `libqcertonlybackend.so`), and the
  `imageformats/` plugins; copy the Wayland platform plugin and its `wayland-shell-integration`
  plugins when `qt6-wayland` is installed, and bundle the `libQt6Wayland*`,
  `libQt6WlShellIntegration`, `libwayland-*` libraries it needs. Add `qt6-wayland` and `patchelf`
  to `packaging/linux/trixie.Dockerfile`; `patchelf` is required for step "RPATH". Do not bundle
  `sqldrivers` or unrelated plugin families.
- Translations: copy `qt_<lang>.qm` for `ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar` (the same
  list as the Windows deploy block) from `QT_INSTALL_TRANSLATIONS`; warn and continue if a file is
  absent so a Qt packaging change is visible, and record the final list.
- `bin/qt.conf` must be written by the script (not by CMake), with exactly:

  ```ini
  [Paths]
  Prefix = ..
  Plugins = plugins
  Translations = translations
  ```

  Relative paths are resolved against the prefix; the file must not contain the build host's
  `lib/x86_64-linux-gnu/qt6` paths.
- RPATH: the installed binary already has `$ORIGIN:$ORIGIN/../lib` from
  `qt_standard_project_setup()`. Qt's own libraries have no RUNPATH and the executable RUNPATH is
  not transitive, so run `patchelf --set-rpath '$ORIGIN'` on every bundled `.so` in `lib/` and
  plugins; do not patch the executable's desktop entry or the system libraries. Skip
  `patchelf` on files that already carry the right RPATH only if the check is explicit.
- `libsecret` is not bundled in this child: the trixie build links `libsecret-1.so.0` and
  `libglib-2.0.so.0`, so the archive needs the target's `libsecret-1-0`/glib (present on desktop
  trixie and Astra). Record this as the archive's system-library requirement; a no-libsecret build
  remains the fallback for systems without it. Do not claim the archive is fully
  distribution-independent beyond its glibc baseline.
- Pack with `tar -C "$root" --owner=0 --group=0 --numeric-owner -czf
  "$output_dir/qtranslate-<version>-linux-x86_64.tar.gz" .`; keep symlinks. Do not delete an
  unrelated user directory: create/clean only the staging directory and the artifact names the
  script owns. Write `SHA256SUMS-linux.txt` in the output directory containing the archive line;
  later children append their artifacts to the same file.
- `packaging/linux/trixie.Dockerfile`: add `patchelf` and `qt6-wayland`; keep the image usable
  for the existing build and tests. The image's `build-essential` already provides `dpkg-dev`, but
  child 03 lists it explicitly for the `.deb` step.

## Done when

- `packaging/linux/package.sh --help` documents all options; missing/invalid arguments and a
  non-trixie host fail with a clear message; a Trixie or container run succeeds from the repository
  root without writing outside `build/` (staging and artifacts only).
- The archive has exactly the layout above: `bin/qtranslate`, `bin/qt.conf` with the three paths,
  `lib/` with the Qt/ICU/pcre2 libraries, `plugins/platforms/libqxcb.so`,
  `plugins/tls/libqopensslbackend.so`, `plugins/imageformats/`, the 13 `translations/qt_*.qm`
  files, the desktop file, and the icon.
- Extracted to a temporary directory, with a temporary `HOME`/`XDG_CONFIG_HOME`, `LD_LIBRARY_PATH`
  unset, and `QT_QPA_PLATFORM=offscreen`, `bin/qtranslate --version` prints
  `QTranslate <version>`; `ldd bin/qtranslate` resolves every Qt library to the extracted `lib/`
  and reports no `not found`; `ldd lib/libQt6Gui.so.6` resolves ICU to the bundle;
  `QT_DEBUG_PLUGINS=1` shows the plugin path under the extracted `plugins/`.
- `desktop-file-validate` passes on the archived desktop file; `tar -tzf` shows the expected
  entries; `sha256sum -c SHA256SUMS-linux.txt` passes from the output directory.
- The script fails clearly when `qtpaths6`, `patchelf`, `cmake`, or `tar` is missing; a Debug build
  directory is rejected with a message asking for Release (its binary is not the release artifact).
- Debian 13 (trixie) still builds Debug and Release with no warnings and CTest 28/28; Astra still
  builds 28/28 after `fly-set-execid.sh`; Windows is unchanged and recorded as not performed.
  No documentation files are edited.

## Risks

- The generic Qt deploy tool does not copy Qt libraries and writes a distro-specific `qt.conf`.
  Do not reintroduce `install(SCRIPT ...)` on Linux; the script's own `qt.conf` is the only one in
  the archive, and `Prefix = ..` with `Plugins = plugins`/`Translations = translations` must be
  verified with `QT_DEBUG_PLUGINS=1`, not assumed.
- Qt libraries have no RUNPATH; the executable's `$ORIGIN/../lib` only resolves its direct
  dependencies. Missing `patchelf` on a bundled library or plugin shows up only on a clean
  machine. Check `ldd` for every bundled `.so` and plugin, and keep the list of bundled private
  dependencies explicit.
- The Wayland plugin closure (`libQt6WaylandClient`, `libQt6WlShellIntegration`,
  `libwayland-client`, `libwayland-egl`, `libxkbcommon`) is larger than the X11-only set. Add
  `qt6-wayland` to the image so the archive starts on a pure Wayland session; if its closure makes
  the bundle unstable, record the exact failure and the XWayland fallback instead of silently
  dropping Wayland from the code. Do not bundle `libEGL`/`libGL`: they must match the driver.
- `libsecret-1.so.0` is a direct dependency of the binary. The archive requires `libsecret-1-0`
  and glib on the target; a clean minimal system without them cannot start it. Either keep this
  requirement and record it, or build the archive from a no-libsecret configuration and record
  that choice; do not add a manual dependency list that contradicts the linked libraries.
- `cmake --install` with a Debug build directory would ship an unoptimized binary; reject anything
  whose `CMakeCache.txt` does not say `CMAKE_BUILD_TYPE=Release`. Do not strip the binary in this
  child unless the `.deb` child also agrees on the same policy.
- `tar` must preserve symlinks (`libQt6Core.so.6 -> libQt6Core.so.6.8.2`) and use numeric
  ownership; a plain `cp -r` can dereference symlinks and double the archive. Verify `tar -tzf`
  entries and the extracted `ldd`.
- `--output-dir` could point at a user directory; do not `rm -rf` it. Remove only the known
  artifact names (or a dedicated staging subdirectory), and keep `SHA256SUMS-linux.txt` in the
  output directory.
- The archive name is `qtranslate-<version>-linux-x86_64.tar.gz`; do not reuse the Windows
  `win64`/`SHA256SUMS.txt` names. The release job downloads both artifact sets into one directory
  and the names must not collide.
- Windows cannot be built here; the script and Dockerfile are Linux-only, but review the diff and
  record the Windows 23 tests as not performed.
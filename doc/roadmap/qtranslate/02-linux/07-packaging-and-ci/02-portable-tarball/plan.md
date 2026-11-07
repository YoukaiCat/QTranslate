---
kind: task
status: done
detailed: true
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
- `cmake --install "$build_dir" --prefix "$stage/install"` produces `bin/qtranslate` plus the
  desktop file and icon from child 01. The script copies that tree to the portable root
  `$stage/qtranslate-<version>-linux-x86_64` and adds the bundle:

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
  `QT_INSTALL_PLUGINS`, and `QT_INSTALL_TRANSLATIONS` (Qt 6.8.2 in trixie), cross-checked against
  the build's `Qt6Core_DIR`. Copy the measured runtime set with symlinks preserved —
  `libQt6Core`, `libQt6Gui`, `libQt6Widgets`, `libQt6Network`, `libQt6DBus`, and `libQt6XcbQpa` —
  not the `libQt6*.so.6*` glob: the trixie image also ships Designer/Qml/Quick libraries from
  `qt6-tools-dev`, which this Widgets application never loads. Add the non-Qt private dependencies
  `libicui18n.so.76`, `libicuuc.so.76`, `libicudata.so.76`, `libdouble-conversion.so.3`,
  `libb2.so.1`, `libpcre2-16.so.0`, `libpcre2-8.so.0`, `libzstd.so.1`, and the Wayland client set
  when the Wayland plugins are copied. Do not bundle glibc, libGL/libEGL/libGLX or other
  driver-critical libraries.
- Plugins: copy `platforms/libqxcb.so`, `platforms/libqoffscreen.so`, `platforms/libqminimal.so`,
  all `tls/` plugins (`libqopensslbackend.so`, `libqcertonlybackend.so`), and all `imageformats/`
  plugins. When `qt6-wayland` is installed, also copy `platforms/libqwayland-generic.so`,
  `platforms/libqwayland-egl.so` (Qt 6.8.2 ships these two, not `libqwayland.so`), all
  `wayland-shell-integration/` plugins, and `wayland-graphics-integration-client/`
  `libqt-plugin-wayland-egl.so` plus `libshm-emulation-server.so`; the dmabuf/vulkan variants are
  not copied because they need libdrm/libvulkan and add nothing to a raster Widgets application.
  Do not bundle `sqldrivers`, `printsupport`, `networkinformation`, `platforminputcontexts`,
  `xcbglintegrations`, the Wayland compositor plugins, or `wayland-decoration-client`.
- Translations: copy `qt_<lang>.qm` for `ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar` (the same
  list as the Windows deploy block) from `QT_INSTALL_TRANSLATIONS`; warn and continue if a file is
  absent so a Qt packaging change is visible, and record the final list. Measured: the bare trixie
  image has no `/usr/share/qt6/translations` directory at all, so the Dockerfile must install
  `qt6-translations-l10n` (283 files, all 13 required names, after installing it); without it every
  copy would warn and the archive would fail this task's layout gate.
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
  `patchelf --set-rpath '$ORIGIN/../../lib'` on every bundled plugin. A plugin's `$ORIGIN` is its
  own directory, and measured `platforms/libqxcb.so` needs `libQt6XcbQpa.so.6`, which the binary
  never loads; with `$ORIGIN` alone that library is found only through the build host's
  `ld.so.cache`. Do not patch the executable or the system libraries.
- `libsecret` is not bundled in this child: the trixie build links `libsecret-1.so.0` and
  `libglib-2.0.so.0`, so the archive needs the target's `libsecret-1-0`/glib (present on desktop
  trixie and Astra). Record this as the archive's system-library requirement; a no-libsecret build
  remains the fallback for systems without it. Do not claim the archive is fully
  distribution-independent beyond its glibc baseline.
- Pack with `tar -C "$stage" --owner=0 --group=0 --numeric-owner -czf
  "$output_dir/qtranslate-<version>-linux-x86_64.tar.gz" "qtranslate-<version>-linux-x86_64"` so
  the archive has one top-level directory named like the artifact; tar keeps symlinks by default.
  Do not delete an unrelated user directory: create/clean only the staging directory and the
  artifact names the script owns. Write `SHA256SUMS-linux.txt` in the output directory containing
  the archive line; later children append their artifacts to the same file.
- `packaging/linux/trixie.Dockerfile`: add `patchelf`, `qt6-wayland`, and
  `qt6-translations-l10n`; keep the image usable for the existing build and tests, and update its
  header comment. The image's `build-essential` already provides `dpkg-dev`, but child 03 lists it
  explicitly for the `.deb` step. Child 06 later replaces the inline list with
  `packaging/linux/trixie-packages.txt`; it must carry the same three packages.

## Steps

1. Install the packaging tools and the Qt runtime pieces in the trixie image.
   - `packaging/linux/trixie.Dockerfile`: add `patchelf`, `qt6-wayland`, and
     `qt6-translations-l10n` to the `apt-get install` list with short comments, and update the
     header comment that currently promises `patchelf` in a later child. Keep `desktop-file-utils`
     from child 01 and every existing entry unchanged.
   - Measured in `qtranslate-trixie:latest` before the change: `patchelf` is absent,
     `/usr/share/qt6/translations` does not exist, and `plugins/platforms/` has no Wayland plugin.
     After `apt-get install patchelf qt6-wayland qt6-translations-l10n` the image has
     `patchelf 0.18.0`, 283 translation files (all 13 required names included), and the Wayland
     platform, shell-integration, and graphics-integration plugins.
   - Rebuild with `docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .` and
     confirm the image still builds and tests the project (step 10).

2. Create `packaging/linux/package.sh` with the CLI contract.
   - `#!/usr/bin/env bash`, `set -euo pipefail`, `umask 022`; make the file executable
     (`chmod +x packaging/linux/package.sh`, mode 100755 in the diff).
   - `die()` prints `package.sh: error: <message>` to stderr and exits 1; `usage_error()` prints
     the same line plus the synopsis and exits 2.
   - `usage()` is a quoted heredoc shared by `--help` and by usage errors: the four options with
     their defaults, the trixie-only host rule, the tools it needs, the artifact and checksum
     names, and the script-owned staging path `<output-dir>/.qtranslate-staging`.
   - Argument loop over `"$@"`: `--build-dir <dir>` and `--output-dir <dir>` require a non-empty
     next value, `--version <X.Y.Z>` is validated with `^[0-9]+\.[0-9]+\.[0-9]+$`, `--help` prints
     the usage and exits 0, and an unknown option or a missing value is a usage error that names
     the argument. The contract is the space-separated form; do not invent a `--name=value` alias.
   - Defaults `build/linux-release` and `build/artifacts-linux`; without `--version` read the
     project version with
     `sed -nE 's/^project\(qtranslate VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt`; an
     empty result is an error that also points at `--version`.
   - Refuse to run outside the repository root (`CMakeLists.txt` and `src/` must both exist) and on
     a non-`x86_64` machine, because the artifact name says `linux-x86_64`.
   - Structure the script as small functions called once from `main` (`prepare_stage`,
     `install_tree`, `write_qt_conf`, `bundle_libraries`, `bundle_plugins`, `bundle_translations`,
     `set_rpaths`, `pack_tarball`, `write_checksums`). Child 03 stages the `.deb` from the install
     root of step 4 and child 04 reuses the bundle root, so the later children add functions
     instead of restructuring the script.

3. Gate the tools, the host, and the build directory before writing anything.
   - Required tools, checked first with `command -v` over a literal list: `cmake`, `qtpaths6`,
     `patchelf`, `tar`, `sha256sum`, `desktop-file-validate`, plus the tools the script itself
     uses (`sed`, `grep`, `tr`, `find`, `mkdir`, `rm`, `cp`, `ln`, `readlink`, `realpath`,
     `dirname`, `basename`, `uname`). One error names every missing tool and the trixie package
     that provides it. Handle `--help` before this check so it works on a bare host.
   - Host: parse `/etc/os-release` with `sed`/`tr` (do not `source` it under `set -u`) and accept
     only `ID=debian` with `VERSION_CODENAME=trixie` or `VERSION_ID=13`. Otherwise print the
     detected ID/version and the exact Docker command built from the values that were passed —
     `docker run --rm -v "$PWD:/src" -w /src qtranslate-trixie ./packaging/linux/package.sh
     --build-dir <build-dir> --output-dir <output-dir>` — then exit 1 before creating a file. The
     Astra mode is child 05; do not add a bypass flag here.
   - Build directory: `CMakeCache.txt` must exist and
     `grep -qx 'CMAKE_BUILD_TYPE:STRING=Release'` must match; otherwise die with the configure
     hint (`cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release
     -DCMAKE_PREFIX_PATH=/usr`). `Debug` and `RelWithDebInfo` are rejected: the archive must ship
     the release binary.
   - Qt consistency: read `Qt6Core_DIR` from the cache with `sed -n 's/^Qt6Core_DIR:[A-Z]*=//p'`;
     two `dirname` calls give the Qt library directory. Compare
     `realpath "<cache-qt-libs>/libQt6Core.so.6"` with the same file below
     `qtpaths6 --query QT_INSTALL_LIBS 2>/dev/null | tail -n1`; a mismatch dies with both paths and
     asks for the Qt that configured the build. This catches the printed container command being
     pointed at an Astra/Qt 6.10 build directory.
   - Resolve `qt_libs`, `qt_plugins`, and `qt_translations` from `qtpaths6`; each must be an
     existing directory. A missing translations directory is an error that names
     `qt6-translations-l10n`, because the base image has none and the layout gate needs the 13
     files. Note that `qtpaths6` writes a UTF-8 locale warning to stderr, so only stdout is read.

4. Stage the install tree and keep the portable root separate.
   - `stage="$output_dir/.qtranslate-staging"`; refuse an empty or `/` output directory,
     `mkdir -p "$output_dir"`, then `rm -rf "$stage"` (that path only) and recreate it. Never touch
     any other entry of `$output_dir`.
   - `install_root="$stage/install"`; run `cmake --install "$build_dir" --prefix "$install_root"`.
     This tree stays bundle-free because child 03 builds the `.deb` from it
     (`usr/bin/qtranslate`, `usr/share/applications`, `usr/share/icons`).
   - Assert `$install_root/bin/qtranslate` is executable and that
     `$install_root/share/applications/qtranslate.desktop` and
     `$install_root/share/icons/hicolor/256x256/apps/qtranslate.png` exist. Assert that
     `$install_root/bin/qt.conf` and `$install_root/share/qt6` do **not** exist; if they do, child
     01's `if(WIN32)` guard regressed and the run must stop instead of shipping a wrong tree.
   - `bundle_root="$stage/qtranslate-$version-linux-x86_64"`; copy the install tree with
     `cp -a "$install_root/." "$bundle_root/"`, which keeps the binary's `$ORIGIN` RUNPATH and the
     file modes.
   - Write `bin/qt.conf` with a quoted heredoc containing exactly `[Paths]`, `Prefix = ..`,
     `Plugins = plugins`, `Translations = translations`; assert the file contains no
     `x86_64-linux-gnu` and no absolute path.
   - Do not strip the binary; `cmake --install --strip` is not used, and child 03 must keep the
     same policy for the `.deb`.

5. Bundle the Qt, ICU, and private libraries with working symlinks.
   - Helper `copy_lib <soname>`: `real=$(readlink -f "$qt_libs/$soname")`, `cp -a "$real"
     "$bundle_root/lib/"`, and if the names differ `ln -sfn "$(basename "$real")"
     "$bundle_root/lib/$soname"`. Copying the SONAME symlink with `cp -a` alone would leave a
     dangling link to a versioned file that was never copied.
   - Qt libraries, measured with `readelf -d`/`ldd`: `libQt6Core.so.6`, `libQt6Gui.so.6`,
     `libQt6Widgets.so.6`, `libQt6Network.so.6`, `libQt6DBus.so.6` are the binary's `NEEDED`
     entries; `libQt6XcbQpa.so.6` is `NEEDED` by `platforms/libqxcb.so` and is not linked by the
     binary. Do not use the `libQt6*` glob for the reason in `## Task`.
   - Private dependencies: `libicui18n.so.76`, `libicuuc.so.76`, `libicudata.so.76`,
     `libdouble-conversion.so.3`, `libb2.so.1`, `libpcre2-16.so.0`, `libpcre2-8.so.0`,
     `libzstd.so.1`.
   - Wayland set, only when the Wayland plugins of step 6 are copied:
     `libQt6WaylandClient.so.6`, `libQt6WaylandEglClientHwIntegration.so.6`,
     `libQt6WlShellIntegration.so.6`, `libQt6OpenGL.so.6` (needed by the graphics-integration
     plugins), `libwayland-client.so.0`, `libwayland-cursor.so.0`, `libwayland-egl.so.1`.
   - Do not bundle glibc/libstdc++, `libGL`, `libEGL`, `libGLX`, `libOpenGL`, X11/XCB,
     fontconfig/freetype/harfbuzz, glib, dbus, OpenSSL, libjpeg, or libproxy; they are the target's
     desktop stack and some must match the driver.
   - After copying, run `ldd` with `LD_LIBRARY_PATH` unset on every file in `lib/` and abort on any
     `not found`. Record the system libraries the bundle still resolves from the host.

6. Copy the plugins and the Qt translations.
   - `mkdir -p "$bundle_root/plugins/platforms"` and copy `libqxcb.so`, `libqoffscreen.so`, and
     `libqminimal.so`; copy all of `tls/` (two files) and all of `imageformats/` (gif, ico, jpeg)
     with their directory names.
   - Wayland, when `"$qt_plugins/platforms/libqwayland-generic.so"` exists: copy
     `libqwayland-generic.so`, `libqwayland-egl.so`, the five `wayland-shell-integration/` plugins,
     and `wayland-graphics-integration-client/libqt-plugin-wayland-egl.so` plus
     `libshm-emulation-server.so`. If the plugin is absent, print one `warning:` that the archive
     will need XWayland and continue; an image without `qt6-wayland` must not fail the run.
   - Do not copy the families excluded in `## Task`; in particular no `sqldrivers` and no
     `wayland-decoration-client`.
   - Translations: loop over `ru es de fr pt_BR it pl uk tr zh_CN ja ko ar`, copy
     `"$qt_translations/qt_<code>.qm"` into `$bundle_root/translations/`, and on a miss print
     `warning: missing Qt translation qt_<code>.qm` and count it. Print `Qt translations: N/13` and
     the sorted list at the end, but do not fail: a Qt packaging change must be visible in the log.
     The loader is `UiLanguages::qtTranslationFile` (`src/core/uilanguages.cpp:101-113`) and
     `AppController::applyLanguage` (`src/app/appcontroller.cpp:383-399`), which tries
     `<appdir>/../translations` first, so these names are the right ones.

7. Set and verify the RUNPATH.
   - For each regular file in `lib/`: `patchelf --set-rpath '$ORIGIN'` (the versioned real file;
     symlinks need no patch).
   - For each `*.so` under `plugins/` (every family is one level deep):
     `patchelf --set-rpath '$ORIGIN/../../lib'`. Measured reason: `libqxcb.so` has
     `NEEDED libQt6XcbQpa.so.6`, which the executable never loads; with `$ORIGIN` alone the loader
     finds it only under the build host's `/usr/lib`, which a clean machine does not have.
   - Do not run `patchelf` on `bin/qtranslate`; assert with `readelf -d` that its RUNPATH is still
     `$ORIGIN:$ORIGIN/../lib`.
   - Verify with `patchelf --print-rpath` for every patched file and with `ldd` (no
     `LD_LIBRARY_PATH`) that the binary, every library, and every plugin resolve their Qt/ICU
     entries inside `$bundle_root`. A path under `/usr/lib` or `/lib` is a failure even though the
     build host would still start the application: `RUNPATH` is searched before `ld.so.cache`, so
     a correct bundle wins on the build host too.

8. Pack the archive and write the checksum.
   - `tar -C "$stage" --owner=0 --group=0 --numeric-owner -czf
     "$output_dir/qtranslate-$version-linux-x86_64.tar.gz" "qtranslate-$version-linux-x86_64"` so
     the archive has one top-level directory named like the artifact. The numeric owner works
     without root, and tar preserves `libQt6Core.so.6 -> libQt6Core.so.6.8.2` by default.
   - `( cd "$output_dir" && sha256sum "qtranslate-$version-linux-x86_64.tar.gz" >
     SHA256SUMS-linux.txt )`: rewrite the file in this child; children 03 and 04 append their
     lines. Never create `SHA256SUMS.txt`.
   - Print a summary the outcome can quote: artifact path and size, the SHA256, the library,
     plugin, and translation counts, whether the Wayland set was bundled, and whether the host,
     tool, Qt, and Release checks passed.

9. Verify the positive path on trixie.
   - Rebuild the image (step 1) and run with the host UID so no root-owned files appear in the
     repository: `docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e LANG=C.UTF-8
     -v "$PWD:/src" -w /src qtranslate-trixie bash -lc '...'` around
     `cmake -S . -B build/trixie-release -G Ninja -DCMAKE_BUILD_TYPE=Release
     -DCMAKE_PREFIX_PATH=/usr`, `cmake --build build/trixie-release --parallel`, and
     `./packaging/linux/package.sh --build-dir build/trixie-release --output-dir
     build/artifacts-linux`.
   - Extract to a temporary directory and run with `LD_LIBRARY_PATH` unset, a temporary
     `HOME`, `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, and a `0700` `XDG_RUNTIME_DIR`, and
     `QT_QPA_PLATFORM=offscreen`: `bin/qtranslate --version` must print `QTranslate <version>` and
     exit 0.
   - Repeat the `ldd` checks of step 7 from the extracted tree; `ldd lib/libQt6Gui.so.6` must
     resolve ICU inside the extract.
   - `QT_DEBUG_PLUGINS=1` with `QT_QPA_PLATFORM=offscreen` must show
     `<extract>/plugins/platforms/libqoffscreen.so`. With `QT_QPA_PLATFORM=wayland` it must show
     `libqwayland-generic.so` loading and then fail on the missing compositor, with no
     `cannot open shared object file`; record the exact line.
   - Translation path proof: write `<config>/qtranslate/qtranslate.ini` with `[app]`,
     `startMinimized=true`, `uiLanguage=de`, run `timeout 5 bin/qtranslate --verbose` with the
     temporary XDG directories, and require `<state>/qtranslate/qtranslate.log` to contain
     `Loaded Qt translation qt_de.qm from <extract>/translations`. This is the check that proves
     the `Translations = translations` line.
   - `desktop-file-validate <extract>/share/applications/qtranslate.desktop` exits 0;
     `tar -tzf` shows the top-level directory and the expected entries; `tar -tzvf` shows the Qt
     SONAME links as links; `sha256sum -c SHA256SUMS-linux.txt` passes from the output directory.
   - Put a sentinel file in the output directory before the run; it must survive, and the only new
     entries are `.qtranslate-staging`, the archive, and the checksum file. Confirm nothing was
     written outside `build/`.

10. Verify the failure paths, the regression, and the records.
    - `--help` exits 0 and lists every option; a missing option value, an unknown option, and
      `--version 1.2` exit non-zero with a `package.sh: error:` line; a missing `CMakeCache.txt`
      and a Debug cache are rejected with the Release hint.
    - On the Astra host, run the script once from the repository root: it must print the detected
      Astra host and the Docker command and exit 1 before creating `build/artifacts-linux`.
    - Demonstrate the missing-tool message with a reduced `PATH` (for example
      `env PATH=/nonexistent /bin/bash packaging/linux/package.sh`); the error must name
      `patchelf` and the other tools, since step 1 adds it to the image.
    - Regression: in the rebuilt trixie image configure Debug and Release in `/tmp`, build with
      `-Wall -Wextra`, grep the logs for `warning:` (none), and run
      `timeout 300 xvfb-run -a ctest --test-dir <build> --output-on-failure` (28/28 in both).
      Astra: the existing `build/linux-debug` and `build/linux-release` trees are untouched; run
      CTest 28/28, and run `sudo /usr/bin/fly-set-execid.sh` first only if something was rebuilt.
      Windows: no MSVC; review the diff and record the 23 tests as not performed.
    - Only `packaging/linux/` changed; `git diff --check` is clean; no README, architecture,
      `AGENTS.md`, handoff, journal, or other map file is edited.
    - Report for the lead: the measured library, plugin, and translation lists with counts, the
      archive size and SHA256, the `ldd`/`QT_DEBUG_PLUGINS`/log evidence, the system requirements
      (`libsecret-1-0`, glib, `libssl3` opened by the TLS backend, `ca-certificates`, the X11 and
      Wayland desktop libraries, `libjpeg62-turbo` for the JPEG plugin), the glibc baseline
      (bundled trixie Qt needs `GLIBC_2.38`, so trixie and later only; Astra is child 05), the
      Wayland decision (bundled, or the exact failure plus the XWayland fallback), and the checks
      that were not performed.

## Done when

- `packaging/linux/package.sh` exists, is executable, and `--help` exits 0 documenting
  `--build-dir`, `--output-dir`, `--version`, and `--help` with their defaults; an unknown option,
  a missing value, an invalid version, a missing `CMakeCache.txt`, and a non-Release build
  directory exit non-zero with a `package.sh: error:` line; on a non-trixie host the script prints
  the Docker command with the values that were passed and exits 1 without writing a file.
- On trixie the run from the repository root produces
  `build/artifacts-linux/qtranslate-<version>-linux-x86_64.tar.gz` and
  `build/artifacts-linux/SHA256SUMS-linux.txt`; nothing is written outside `build/`, and a
  pre-existing unrelated file in the output directory survives.
- The archive has one top-level `qtranslate-<version>-linux-x86_64/` directory with
  `bin/qtranslate`; `bin/qt.conf` holding exactly `Prefix = ..`, `Plugins = plugins`,
  `Translations = translations` and no host path; `lib/` with `libQt6Core`, `libQt6Gui`,
  `libQt6Widgets`, `libQt6Network`, `libQt6DBus`, `libQt6XcbQpa` (as `libQt6*.so.6` links to the
  versioned files), ICU 76, double-conversion, b2, pcre2-16/8, and zstd; `plugins/platforms/` with
  `libqxcb.so`, `libqoffscreen.so`, `libqminimal.so`; `plugins/tls/` with both backends;
  `plugins/imageformats/` with gif, ico, and jpeg; `translations/` with the 13 `qt_<lang>.qm`
  files; `share/applications/qtranslate.desktop`; and
  `share/icons/hicolor/256x256/apps/qtranslate.png`. No `sqldrivers`, no `share/qt6`, and no
  second `qt.conf`.
- With `qt6-wayland` in the image the archive also contains
  `plugins/platforms/libqwayland-generic.so`, `libqwayland-egl.so`, the five
  `wayland-shell-integration` plugins, the two graphics-integration plugins, and
  `libQt6WaylandClient`, `libQt6WaylandEglClientHwIntegration`, `libQt6WlShellIntegration`,
  `libQt6OpenGL`, `libwayland-client`, `libwayland-cursor`, and `libwayland-egl`; if Wayland is
  not bundled, the exact reason and the XWayland fallback are recorded instead of dropping it
  silently.
- `patchelf --print-rpath` reports `$ORIGIN` for every file in `lib/` and `$ORIGIN/../../lib` for
  every plugin, and `bin/qtranslate` keeps `$ORIGIN:$ORIGIN/../lib`.
- Extracted with `LD_LIBRARY_PATH` unset and temporary `HOME`/`XDG_CONFIG_HOME`/`XDG_STATE_HOME`/
  `XDG_RUNTIME_DIR`, `QT_QPA_PLATFORM=offscreen bin/qtranslate --version` prints
  `QTranslate <version>`; `ldd` reports no `not found` for the binary, every bundled library, and
  every plugin, and resolves the Qt/ICU entries inside the extract; `QT_DEBUG_PLUGINS=1` shows the
  offscreen plugin under `<extract>/plugins/platforms/`; a run with `[app] uiLanguage=de` and
  `--verbose` logs `Loaded Qt translation qt_de.qm from <extract>/translations`.
- `desktop-file-validate` passes on the archived desktop file; `tar -tzvf` shows the Qt SONAME
  symlinks as symlinks; `sha256sum -c SHA256SUMS-linux.txt` passes from the output directory and
  the file lists only the archive for now.
- A missing `qtpaths6`, `patchelf`, `cmake`, `tar`, `sha256sum`, or `desktop-file-validate`
  produces one clear error naming it; `patchelf` is installed by the rebuilt image.
- `packaging/linux/trixie.Dockerfile` installs `patchelf`, `qt6-wayland`, and
  `qt6-translations-l10n`; the image rebuilds, and Debian 13 (trixie) still builds Debug and
  Release with no warnings and CTest 28/28. Astra reports 28/28 in its existing build trees (after
  `fly-set-execid.sh` if anything was rebuilt); Windows Debug/Release and the 23 tests are
  recorded as not performed. No documentation files are edited, and the outcome carries the
  measured lists, the archive size and SHA256, the system-library requirements, and the trixie
  glibc baseline caveat.

## Risks

- The generic Qt deploy tool does not copy Qt libraries and writes a distro-specific `qt.conf`.
  Do not reintroduce `install(SCRIPT ...)` on Linux; the script's own `qt.conf` is the only one in
  the archive, and `Prefix = ..` with `Plugins = plugins`/`Translations = translations` must be
  verified with `QT_DEBUG_PLUGINS=1`, not assumed.
- Qt libraries have no RUNPATH; the executable's `$ORIGIN/../lib` only resolves its direct
  dependencies. Measured: a plugin patched with `$ORIGIN` alone still resolves
  `libQt6XcbQpa.so.6` from `/lib/x86_64-linux-gnu`, because the loader falls through to the build
  host's `ld.so.cache`; with `$ORIGIN/../../lib` it resolves inside the bundle. Check the resolved
  paths, not just the exit code, for every bundled `.so` and plugin, and keep the list of bundled
  private dependencies explicit.
- The Wayland closure was measured: two platform plugins (`libqwayland-generic.so`,
  `libqwayland-egl.so`), five `wayland-shell-integration` plugins, two graphics-integration
  plugins (they need `libQt6OpenGL.so.6`), `libQt6WaylandClient`,
  `libQt6WaylandEglClientHwIntegration`, `libQt6WlShellIntegration`, `libwayland-client`,
  `libwayland-cursor`, and `libwayland-egl`. Add `qt6-wayland` to the image so the archive can
  start on a pure Wayland session. The packaging image has no compositor, so a real Wayland start
  is not performed; the check is that the plugin loads (no missing shared object) and the XWayland
  fallback stays documented. If the set proves unstable, record the exact failure and the fallback
  instead of dropping Wayland from the code. Do not bundle `libEGL`/`libGL`:
  `libqwayland-egl.so` needs the driver's EGL.
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
- The trixie image is not a clean system: it has the full Qt and an `ld.so.cache`, so a missing
  RUNPATH or a wrong `qt.conf` can still start the application there. The gates are the resolved
  `ldd` paths, `QT_DEBUG_PLUGINS=1`, and the translation log line; a real clean trixie machine is
  not available and the archive is not claimed to be distribution-independent beyond its glibc
  baseline.
- Measured image facts to respect: `qt6-tools-dev` adds Designer/Qml/Quick libraries, so a
  `libQt6*.so.6*` glob would ship ~26 libraries instead of the measured six; `qtpaths6` prints a
  UTF-8 locale warning to stderr, so only its stdout is read. `ldd` prints `/lib/x86_64-linux-gnu`
  while `qtpaths6` prints `/usr/lib/x86_64-linux-gnu` (usrmerge): compare with `realpath`, not with
  string prefixes.
- System dependencies the bundle does not carry and that `ldd` cannot fully show: the TLS backend
  opens `libssl3`/`libcrypto3` at runtime (no `NEEDED` entry), the JPEG image plugin needs
  `libjpeg62-turbo`, `libQt6Gui` needs glib and `libdbus-1`, and the X11/Wayland desktop libraries
  (xcb, xkbcommon, fontconfig/freetype/harfbuzz, libEGL from the driver) must be the target's. Do
  not bundle OpenSSL (security updates must come from the distribution); record these requirements
  instead of claiming a self-contained archive.
- Docker runs the script as root by default and would leave root-owned files in `build/`. Run the
  verification with `--user "$(id -u):$(id -g)"` and `-e HOME=/tmp`; the script itself must not
  require root and must not write outside the output directory. The printed host-check command
  keeps the contract's `docker run` shape.
- Keep the install tree and the portable root separate (`stage/install` vs
  `stage/qtranslate-<version>-linux-x86_64`). Child 03 stages the `.deb` from the install tree and
  must not inherit `lib/`, `plugins/`, `translations/`, or `bin/qt.conf`; merging the two trees
  now would force a rewrite there.

## Outcome

- `packaging/linux/package.sh` exists (mode 100755, 602 lines, `set -euo pipefail`, `umask 022`) and
  implements the contract: `--build-dir`/`--output-dir`/`--version`/`--help` with the documented
  defaults, space-separated values only, `die()` → `package.sh: error:` and exit 1, usage errors exit
  2; the repository-root, host, tool, architecture, `CMAKE_BUILD_TYPE:STRING=Release`, and Qt checks
  run before any file is written; the host gate reads `/etc/os-release` with `sed`/`tr` and refuses
  anything except `ID=debian` with `trixie`/`13`, printing the `docker run --rm -v "$PWD:/src" -w
  /src qtranslate-trixie ...` command built from the passed values (shell-quoted with `%q`, plus
  `--version` when it was given) and creating nothing. The staging layout is
  `<output-dir>/.qtranslate-staging` with `install/` (the bundle-free tree for child 03) and
  `qtranslate-<version>-linux-x86_64/`; only the staging directory is removed, and a pre-existing
  unrelated file in the output directory survives every run.
- `packaging/linux/trixie.Dockerfile` adds `patchelf`, `qt6-wayland`, and `qt6-translations-l10n`
  with short comments and updates the header; every earlier entry, including `desktop-file-utils`,
  is unchanged. The rebuilt `qtranslate-trixie` image has `patchelf 0.18.0`, 283 Qt translation
  files with all 13 required names, and the Wayland platform/shell-integration/
  graphics-integration plugins, and still builds and tests the project.
- The bundle is the measured set, not a glob: 21 versioned libraries with SONAME symlinks — the six
  `libQt6*` (`Core`, `Gui`, `Widgets`, `Network`, `DBus`, `XcbQpa`), ICU 76
  (`libicui18n`/`libicuuc`/`libicudata`), `libdouble-conversion.so.3`, `libb2.so.1`,
  `libpcre2-16.so.0`, `libpcre2-8.so.0`, `libzstd.so.1`, and the Wayland set
  (`libQt6WaylandClient`, `libQt6WaylandEglClientHwIntegration`, `libQt6WlShellIntegration`,
  `libQt6OpenGL`, `libwayland-client`, `libwayland-cursor`, `libwayland-egl`); 17 plugins
  (`platforms/` xcb/offscreen/minimal/wayland-generic/wayland-egl, both `tls/` backends, gif/ico/jpeg
  image formats, five `wayland-shell-integration/` plugins and the two graphics-integration
  plugins); 13 `qt_<lang>.qm` files plus the 26 `qtbase_<lang>.qm`/`qtmultimedia_<lang>.qm`
  dependency catalogs. `qt.conf` is written by the script with exactly `Prefix = ..`,
  `Plugins = plugins`, `Translations = translations` and no host path; `patchelf --set-rpath
  '$ORIGIN'` is applied to every file in `lib/`, `'$ORIGIN/../../lib'` to every plugin, and the
  executable is untouched (`RUNPATH` stays `$ORIGIN:$ORIGIN/../lib`).
- Decision found in the work: in Qt 6.8.2 the 13 `qt_<lang>.qm` files are 91-102-byte meta-catalogs
  (`qt_de.qm` references `qtbase_de` and `qtmultimedia_de`), so `QTranslator::load` fails without
  the dependency catalogs and the required `Loaded Qt translation "qt_de.qm" from
  "<extract>/translations"` log line is unreachable. The script copies those two catalogs per
  language as well, warns and continues on any missing file, and keeps `Qt translations: 13/13` as
  the gate. Other deviations from the literal plan: the host gate runs before the tool gate so an
  Astra run prints the docker command despite the missing `patchelf`; the tool list also names
  `readelf`, `ldd`, `cat`, `tail`, `sort`, and `stat`, which the script uses; and an extra Qt
  symbol-version check (`readelf --version-info`, e.g. `Qt_6.10` vs `Qt_6.8`) catches an Astra
  build dir, because the plan's `realpath` comparison cannot distinguish the two Qt installations
  (both under `/usr/lib/x86_64-linux-gnu`).
- Artifact from the final run: `build/artifacts-linux/qtranslate-0.11.0-linux-x86_64.tar.gz`,
  28 637 141 bytes, SHA256
  `c978159ca0737bb3b0b882c1d6edfa728fde2e1149bd187fcd73917ce4e794d5`, with
  `SHA256SUMS-linux.txt` holding that line; `sha256sum -c` passes from the output directory. The
  archive has 118 entries, one top-level `qtranslate-0.11.0-linux-x86_64/` directory, numeric
  ownership, and the Qt SONAME/version links preserved as symlinks (`libQt6Core.so.6 ->
  libQt6Core.so.6.8.2`, `libicuuc.so.76 -> libicuuc.so.76.1`). The bytes are not reproducible
  across runs because `patchelf` updates library mtimes and tar records them; the recorded SHA
  belongs to this run, and the plan does not require reproducibility.
- Verification in `debian:trixie` (Qt 6.8.2, GCC 14.2): Debug and Release rebuild in `/tmp` with no
  `-Wall -Wextra` warnings and CTest 28/28 in both; the Release tree `build/trixie-release` packages
  cleanly. Astra Linux 1.8.7 (Qt 6.10.2) reports 28/28 in the existing `build/linux-debug` and
  `build/linux-release` trees; nothing was rebuilt, so `fly-set-execid.sh` was not needed.
- Extracted-archive evidence (with `LD_LIBRARY_PATH` unset, temporary `HOME`/XDG dirs and
  `QT_QPA_PLATFORM=offscreen`): `bin/qtranslate --version` prints `QTranslate 0.11.0`; `ldd` reports
  no `not found` for the binary, every bundled library, and every plugin, and resolves Qt/ICU inside
  the extract (`libicui18n.so.76 => <extract>/lib/libicui18n.so.76`); `patchelf --print-rpath`
  reports `$ORIGIN` for the libraries and `$ORIGIN/../../lib` for the plugins;
  `QT_DEBUG_PLUGINS=1` shows `<extract>/plugins/platforms/libqoffscreen.so`, and
  `QT_QPA_PLATFORM=wayland` loads `libqwayland-generic.so` and then fails only on
  `Failed to create wl_display`, with no `cannot open shared object file`; the `uiLanguage=de` +
  `--verbose` run logs `Loaded Qt translation "qt_de.qm" from "<extract>/translations"`;
  `desktop-file-validate` exits 0 on the archived desktop file. On the Astra host the same archive
  fails before `main` with `version 'GLIBC_2.38' not found` from the bundled trixie Qt, which is the
  documented baseline fact, not a packaging defect.
- Negative paths: `--help` exits 0; an unknown option, a missing value, and `--version 1.2` exit 2
  with `package.sh: error:`; a missing `CMakeCache.txt` and a Debug cache exit 1 with the Release
  configure hint; a reduced `PATH` produces one message naming every missing tool with its trixie
  package (including `patchelf`); the Astra host gate exits 1 with the docker command (and
  `--version` only when it was passed) without creating the output directory; a spaced path is
  quoted so the printed command copies correctly.
- System requirements recorded for the archive: `libsecret-1-0` and glib are direct `NEEDED`
  dependencies of the binary; `libssl3`/`libcrypto3` and `ca-certificates` for the OpenSSL TLS
  backend; the X11 stack (X11/XCB/XTest/xkbcommon/SM/ICE) and the Wayland driver libraries
  (`libEGL`, `libGLX`, `libOpenGL`, `libGLdispatch`); fontconfig/freetype/harfbuzz/graphite2/
  libpng/zlib; `libjpeg62-turbo` for the JPEG plugin; `libdbus-1`, `libproxy` and the rest of the
  target's network stack. The script prints the measured list (`System libraries resolved from the
  host:`, 94 names). Bundled trixie Qt needs `GLIBC_2.38`, so the archive runs on trixie and later,
  not on Astra 1.8.7 (glibc 2.36); the trixie archive is never claimed to be
  distribution-independent beyond that baseline. The no-libsecret build stays the fallback.
- Not performed: Windows Debug/Release and the 23 Windows tests (no MSVC; the change is Linux-only
  and was reviewed by diff); a real Wayland session (no compositor in the image; plugin-load only),
  a run on a clean machine (the trixie image has the full Qt and an `ld.so.cache`; the gates were the
  resolved `ldd` paths, `QT_DEBUG_PLUGINS=1` and the translation log line), and a run of the trixie
  archive on Astra (glibc baseline; child 05 builds the native Astra artifact).
- Notes for later children: the `.deb` staging root (`$stage/install`) is ready and bundle-free;
  child 06's `trixie-packages.txt` must carry `patchelf`, `qt6-wayland`, and
  `qt6-translations-l10n` in addition to the existing entries; CTest run as uid 1000 in the current
  image fails `tst_hotkeymanager_wayland` because the image has no `/etc/passwd` entry and
  `dbus-daemon` refuses (CI runs as root, or the image needs a user), and the `ldd` gate
  intentionally enforces only Qt/ICU inside the bundle while other system libraries are recorded;
  per-file assertions for the `tls`/`imageformats`/`wayland-shell-integration` plugin names are not
  implemented, so a package change that drops one plugin after the directory check would not warn.

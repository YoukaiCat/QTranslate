---
kind: task
status: done
detailed: true
depends: [02-portable-tarball, 03-deb-package, 04-appimage]
---

# Astra Linux artifacts

## Task

Build and verify the mandatory Astra Linux 1.8.7 artifacts natively, because the trixie artifacts
cannot serve Astra: the trixie `.deb` needs `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)` (Astra has
`libglib2.0-0` 2.74.6 and `libxtst6` 2:1.2.3), and the trixie tar.gz/AppImage bundle Qt 6.8.2,
whose `libQt6Gui`/`libQt6Network` require `GLIBC_2.38` (Astra has 2.36). The Astra executable
itself is compatible (`GLIBC_2.34`, `GLIBCXX_3.4.14`), so the native build is the correct path.

Contract:

- Add an Astra mode to `packaging/linux/package.sh` (auto-detect `ID=astra` in `/etc/os-release` or
  an explicit `--target astra`). The Trixie host check from child 02 must not block this mode; the
  Astra mode must not run the trixie-only artifact names. The same bundle logic, `qt.conf`, and
  RPATH rules are reused with Astra's Qt 6.10.2 (`qtpaths6` from `/usr`).
- Build from the existing native Release tree `build/linux-release` (Qt 6.10.2, GCC 12.2). Before
  packaging, run `sudo /usr/bin/fly-set-execid.sh` after every rebuild and run CTest; expect 28/28
  with 0 warnings. If the launch check needs a fresh build, rebuild and repeat the trusted-client
  step; a fresh tree otherwise QSKIPs two X11 tests.
- Produce in the output directory:
  - `qtranslate-<version>-linux-x86_64-astra.tar.gz` — the same layout as child 02, with Astra's
    Qt 6.10.2 libraries, plugins, Qt translations, `bin/qt.conf`, the desktop file, and the icon.
  - `qtranslate_<version>+astra1_amd64.deb` — `Version: <version>+astra1` (Debian version syntax;
    the application version itself is unchanged), `Architecture: amd64`, the same three installed
    files, and `Depends` from Astra's `dpkg-shlibdeps` on the staged binary. Expect Astra's package
    names (`libqt6core6t64`, `libqt6gui6`, `libqt6widgets6`, `libqt6network6`, `libqt6dbus6`,
    `libsecret-1-0`, `libglib2.0-0`, `libx11-6`, `libxtst6`, `libc6 (>= 2.34)`), not the trixie
    `libglib2.0-0t64`/`libxtst6 (>= 2:1.2.5)` pair.
  - `SHA256SUMS-linux-astra.txt` with both Astra artifacts (do not overwrite
    `SHA256SUMS-linux.txt` from the trixie run; keep the file names distinct because both sets may
    be collected for a release).
- Verify on Astra:
  - The application builds and runs from the native Release tree; `qtranslate --version` and the
    GUI under the real X11 `:0` session start; the three hotkeys and CLI commands still work (the
    existing 28-test suite is the automated gate).
  - The Astra tar.gz extracted to a temporary directory with `LD_LIBRARY_PATH` unset and a
    temporary `HOME` runs `bin/qtranslate --version`; `ldd` resolves Qt to the extracted `lib/`
    (Astra Qt 6.10.2), not `/usr/lib`.
  - The Astra `.deb` installs with `sudo dpkg -i`, `dpkg -L qtranslate` lists the files,
    `desktop-file-validate /usr/share/applications/qtranslate.desktop` passes,
    `xvfb-run -a qtranslate --version` prints the version, the GUI starts under `:0`, the desktop
    entry/icon are visible, the autostart file is written/removed from the app, and the settings
    and log paths are under the XDG directories. Remove it with `sudo dpkg -r qtranslate`.
  - If `sudo` is unavailable or the user must run the commands, write the exact commands and the
    reason into the outcome and mark the installation check as not performed, never as passed.
- Record the trixie artifacts' Astra failure with evidence:
  - Run the trixie `.deb`'s `dpkg -i` on Astra and capture the exact unsatisfied dependencies
    (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`); do not "fix" the trixie package.
  - Extract the trixie tar.gz (and AppImage if built) on Astra and capture the loader error
    (`libQt6Gui.so.6: version GLIBC_2.38 not found` or the exact equivalent). This proves why the
    native Astra build is required and is reported to the lead.
- Do not publish the Astra artifacts, tag, or push. They are local until the user approves a
  release; the CI publishes only the trixie artifacts (child 06). Report the exact names, sizes,
  and checksums.

## Steps

1. Install the one missing host tool and record the Astra environment.
   - Measured on this host: `/etc/os-release` has `ID=astra`, `ID_LIKE=debian`,
     `VERSION_ID=1.8_x86-64`, `VERSION_CODENAME=1.8_x86-64` (the distro is Astra Linux 1.8.7, but
     the file does not contain `1.8.7`; the gate must key on `ID=astra`). `glibc` 2.36,
     `qtpaths6`/`qmake6` 6.10.2, ICU 72, GCC 12.2, `dpkg-shlibdeps` 1.21.22
     (`dpkg-dev` 1.21.22.astra.se9), `dpkg-deb`, `desktop-file-validate`, `readelf`, `ldd`, `du`,
     `curl`, `xvfb-run`, `xdotool`, `xwininfo`, `gtk-launch`, and `kate` are present; `qtranslate`
     is not installed; `build/linux-debug` and `build/linux-release` exist and list 28 tests each;
     no `qtranslate` process runs.
   - `patchelf` is absent (measured). Install it with `sudo apt-get install -y patchelf` (candidate
     0.14.3-1+b1 from the Astra 1.8 repository) and record `dpkg-query -W patchelf`. Do not install
     anything else; the Astra run needs no `curl` and no AppImage tools.
   - `build/artifacts-linux` already holds the trixie run from child 04:
     `qtranslate-0.11.0-linux-x86_64.tar.gz` (28 636 427 bytes, SHA256 `bad8a16f…38f3`),
     `qtranslate_0.11.0_amd64.deb` (406 188 bytes, SHA256 `677be95a…2e82`),
     `qtranslate-0.11.0-x86_64.AppImage` (39 270 904 bytes, SHA256 `e46dd917…1e45`),
     `SHA256SUMS-linux.txt` (SHA256 `b6a9435f4e5407179595568360345f446ba7403dccfaba84d52a57682633b839`),
     the `.qtranslate-staging` directory, and `sentinel.txt`. Record these values and the file list;
     they are the before/after reference for step 5.
   - Keep the container run for step 9: Docker works and `qtranslate-trixie:latest` is present; the
     AppImage tool cache `build/linux-tools/appimage` is populated (four files).

2. Add the `--target` option and the Astra artifact names to `packaging/linux/package.sh`.
   - Globals (`packaging/linux/package.sh:20-53`): add `target=trixie`, `deb_version=`,
     `bundle_name=`, `tarball_name=`, `checksums_file=SHA256SUMS-linux.txt`, and a host label for
     the summary. `appimage_enabled` stays as is.
   - Naming in `resolve_version` (`packaging/linux/package.sh:241-248`), after `$version` is
     resolved or validated:
     - `trixie`: `deb_version=$version`, `bundle_name=qtranslate-$version-linux-x86_64`,
       `tarball_name=$bundle_name.tar.gz`, `checksums_file=SHA256SUMS-linux.txt`.
     - `astra`: `deb_version=$version+astra1`, `bundle_name=qtranslate-$version-linux-x86_64-astra`,
       the same `tarball_name=$bundle_name.tar.gz`, `checksums_file=SHA256SUMS-linux-astra.txt`.
       `--version` keeps the `^[0-9]+\.[0-9]+\.[0-9]+$` regex: `+astra1` is generated, never passed
       in, and `AppInfo::version()`/`CMakeLists.txt` stay `0.11.0`.
   - `parse_args` (`packaging/linux/package.sh:105-141`): add `--target <trixie|astra>` with the
     space-separated form, an invalid value is a usage error; after the loop
     `if [[ $target == astra ]]; then appimage_enabled=0; fi`, so the order of `--target` and
     `--skip-appimage` does not matter and `--skip-appimage` stays accepted (redundant) in Astra
     mode. Do not auto-detect the Astra target from `/etc/os-release`: an explicit flag is the
     decision, so a run on Astra cannot silently switch artifact names, and a `--target trixie` run
     keeps the trixie gate and the printed docker command.
   - `usage()` (`packaging/linux/package.sh:60-97`): document `--target`, the Astra artifact names,
     `--target astra` skipping the AppImage and requiring an Astra Linux host, and that the Astra
     names/checksum file are distinct from the trixie ones.
   - `install_tree` (`packaging/linux/package.sh:393-416`): `bundle_root="$stage/$bundle_name"`
     instead of the literal; keep the install-tree assertions and the bundle-free checks unchanged.
   - `pack_tarball` (`packaging/linux/package.sh:647-664`): `artifact="$output_dir/$tarball_name"`,
     pack `"$bundle_name"`, and assert the single top-level entry equals `$bundle_name`.
   - `build_deb` (`packaging/linux/package.sh:666-831`):
     `deb_artifact="$output_dir/qtranslate_${deb_version}_amd64.deb"`, `Version: $deb_version` in
     `DEBIAN/control`, and the `dpkg-deb --field Version` self-check against `$deb_version`. Keep the
     `Package`, `Architecture: amd64`, `Maintainer`, `Installed-Size`, `Homepage`, and `Description`
     fields, the `dpkg-shlibdeps` source control stanza, the three-file layout, the unstripped binary
     hash equality, and the `./control`-only control archive unchanged.
   - `write_checksums` (`packaging/linux/package.sh:1000-1022`): write `$checksums_file` with
     `$tarball_name` and `qtranslate_${deb_version}_amd64.deb` (plus the AppImage in trixie mode).
     In Astra mode do not print the foreign-AppImage warning: the trixie AppImage in the shared
     output directory is legitimate and is already listed in `SHA256SUMS-linux.txt`; the warning
     text would be wrong there. In trixie mode keep the existing warning.
   - `print_summary` (`packaging/linux/package.sh:1024-1058`): print the parameterized names and
     `appimage: skipped (Astra target)` vs `skipped (--skip-appimage)`, and a `host check:` line
     that distinguishes `Debian 13 (trixie)` from Astra Linux (astra target).

3. Gate the Astra host without weakening the trixie gate.
   - `check_host` (`packaging/linux/package.sh:148-184`), before `check_tools`:
     - `target=astra`: accept only `ID=astra`; do not require a version string (the host reports
       `VERSION_ID=1.8_x86-64`). Otherwise die with the detected `ID`/`VERSION_ID` and state that
       the native Astra artifacts must be built on Astra Linux, not in the `debian:trixie` image;
       write nothing.
     - `target=trixie`: keep the current Debian 13 gate and the exact `docker run --rm -v
       "$PWD:/src" -w /src qtranslate-trixie ./packaging/linux/package.sh ...` message. On an Astra
       host add one hint line after the docker command:
       `For the native Astra artifacts run this script on Astra Linux 1.8.7 with --target astra`
       (with the passed `--build-dir`/`--output-dir`/`--version` values; do not add `--target
       trixie` to the printed docker command). The `--skip-appimage`/`--version` quoting stays.
   - `check_tools` (`packaging/linux/package.sh:186-239`) and `check_qt`
     (`packaging/linux/package.sh:266-321`) need no change: `patchelf` from step 1 satisfies the
     tool gate, `curl` is not required in Astra mode, and the Qt check must pass with `/usr` Qt
     6.10.2 (the `Qt_6.10` symbol-version comparison against `qtpaths6`). `qtpaths6` writes a UTF-8
     locale warning to stderr; read only stdout.

4. Make the bundle content work with Astra's Qt 6.10.2.
   - `bundle_libraries` (`packaging/linux/package.sh:446-492`): the ICU triple is hardcoded to
     `.so.76`, but Astra ships ICU 72 (`libicui18n.so.72`, `libicuuc.so.72`, `libicudata.so.72`;
     measured in `ldd` and in `libQt6Core.so.6`'s `NEEDED`). Read the major from
     `readelf -d "$qt_libs/libQt6Core.so.6" | sed -n 's/.*libicui18n\.so\.\([0-9][0-9]*\).*/\1/p' | head -n1`
     and use `libicui18n.so.$icu_major`, `libicuuc.so.$icu_major`, `libicudata.so.$icu_major`;
     die if the major cannot be read. Keep the Qt set (`Core`, `Gui`, `Widgets`, `Network`, `DBus`,
     `XcbQpa`) and the other private names (`libdouble-conversion.so.3`, `libb2.so.1`,
     `libpcre2-16.so.0`, `libpcre2-8.so.0`, `libzstd.so.1`); all exist on Astra and trixie. The
     Wayland set stays conditional on `libqwayland-generic.so`; the file is absent on Astra (see
     the next bullet), so the existing warning and `wayland_bundled=0` are the expected result.
   - `bundle_plugins` (`packaging/linux/package.sh:494-537`): replace the "copy all of
     `imageformats/`" loop with an explicit `libqgif.so libqico.so libqjpeg.so` list and die if one
     is missing. Measured: trixie's imageformats has exactly these three; Astra's Qt 6.10.2 has 11,
     and `libqpdf.so` needs `libQt6Pdf.so.6` while `libqsvg.so` needs `libQt6Svg.so.6`; both Qt
     libraries are not bundled, and `check_ldd` (`packaging/linux/package.sh:577-606`) rejects any
     `libQt6*` resolved outside the bundle. The application never loads PDF/SVG images (its icon is
     PNG), so the measured three-plugin set is enough and keeps the trixie layout identical. Keep
     `tls/` as the whole directory (two backends on both targets) and the three platform plugins
     `libqxcb.so`, `libqoffscreen.so`, `libqminimal.so` (all present on Astra).
   - Wayland on Astra: Qt 6.10.2 names its Wayland platform plugin `platforms/libqwayland.so` and
     ships no `libqwayland-generic.so`/`libqwayland-egl.so`, so the existing check does not match,
     the script prints the XWayland warning, and the Astra archive contains no Wayland plugin.
     Record the XWayland fallback; do not add a second Qt 6.10-specific Wayland path (X11 is the
     full Astra target, Wayland stays limited per the stage).
   - `libmd4c.so.0`: measured, `libQt6Gui.so.6` has it in `NEEDED` on both trixie and Astra. Keep it
     a system library (do not bundle): on a clean Astra it comes from `libmd4c0` 0.4.8
     (`/usr/lib/x86_64-linux-gnu/libmd4c.so.0`); on this development host the loader currently
     picks the older local build 0.5.2 from `/usr/local/lib`. Record this in the system-library
     list and do not claim the archive is self-contained beyond its glibc baseline.
   - `write_qt_conf` (`packaging/linux/package.sh:418-429`) and `set_rpaths`
     (`packaging/linux/package.sh:608-645`) stay unchanged; on Astra the strict in-bundle check
     must still resolve every `libQt6*`/`libicu*` inside `$bundle_real`.

5. Build the Astra artifacts from the existing native Release tree.
   - Preconditions: `pgrep -a qtranslate` is empty; `git status` is clean; `build/linux-release` is
     a `CMAKE_BUILD_TYPE=Release` tree with `bin/qtranslate`; run
     `ctest --test-dir build/linux-release -N` (28 tests) and the full CTest in Release and Debug.
     No rebuild is expected; if a rebuild happens, run `sudo /usr/bin/fly-set-execid.sh` before the
     X11 tests. `cmake --install` does not rebuild.
   - Run from the repository root:
     `./packaging/linux/package.sh --target astra --build-dir build/linux-release --output-dir build/artifacts-linux`.
     Using the same output directory as the trixie run is deliberate: it proves that the new names
     and the Astra checksum file do not collide with the trixie files.
   - Record the before/after `sha256sum build/artifacts-linux/SHA256SUMS-linux.txt` (expected
     unchanged, `b6a9435f…`) and the before/after checksums of the three trixie artifacts. The
     expected new entries are `qtranslate-0.11.0-linux-x86_64-astra.tar.gz`,
     `qtranslate_0.11.0+astra1_amd64.deb`, and `SHA256SUMS-linux-astra.txt`; `sentinel.txt` and the
     three trixie artifacts must survive; the only new directory is `.qtranslate-staging`; nothing
     is written outside `build/`. Record sizes and SHA256 values.
   - Inspect the `.deb` and record the exact output of `dpkg-deb --info`, `--contents`,
     `--ctrl-tarfile` (only `./control`), and `--field Version,Depends,Installed-Size,Maintainer`.
     The measured `Depends` for the current binary is
     `libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0 (>= 2.12.0), libqt6core6t64 (>= 6.10.2),
     libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2), libqt6network6 (>= 6.7.2),
     libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5), libx11-6, libxtst6`;
     record the run's exact line. In Astra mode assert `libglib2.0-0 (` and `libqt6core6t64` are
     present and fail if `libglib2.0-0t64` or `libxtst6 (>=` appears (use a regex for
     `libglib2.0-0 (` so the check cannot match `libglib2.0-0t64`). Keep the existing
     `libc6 (>= 2.34)`/`libqt6dbus6`/`libxtst6` assertions and the conditional `libsecret-1-0`
     check (the binary links `libsecret-1.so.0`, so the entry must be present).
   - Check `Version: 0.11.0+astra1`, `Architecture: amd64`, exactly three regular files, and no
     `usr/bin/qt.conf`/`usr/lib`/`usr/plugins`/`usr/translations` in the package. Run
     `sha256sum -c SHA256SUMS-linux-astra.txt` from `build/artifacts-linux` and
     `desktop-file-validate` on the desktop file inside the archive. Confirm `tar -tzvf` keeps the
     Qt SONAME symlinks and the top-level directory `qtranslate-0.11.0-linux-x86_64-astra/`.

6. Verify the Astra tar.gz at runtime.
   - Extract to a temporary directory outside the repository (for example
     `/tmp/opencode/astra-tar`), with `LD_LIBRARY_PATH` unset, temporary `HOME`, `XDG_CONFIG_HOME`,
     `XDG_STATE_HOME`, a `0700` `XDG_RUNTIME_DIR`, and `QT_QPA_PLATFORM=offscreen`:
     `bin/qtranslate --version` must print `QTranslate 0.11.0` and exit 0.
   - `ldd` on `bin/qtranslate`, every file in `lib/`, and every plugin: no `not found`, and the
     Qt/ICU entries resolve inside the extract (Astra Qt 6.10.2, ICU 72). `patchelf --print-rpath`
     reports `$ORIGIN` for the libraries and `$ORIGIN/../../lib` for the plugins; `readelf -d
     bin/qtranslate` keeps `$ORIGIN:$ORIGIN/../lib`; `bin/qt.conf` has exactly `Prefix = ..`,
     `Plugins = plugins`, `Translations = translations` and no host path.
   - `QT_DEBUG_PLUGINS=1` with the offscreen platform must show
     `<extract>/plugins/platforms/libqoffscreen.so`. With `[app] uiLanguage=de`,
     `startMinimized=true`, and `--verbose`, the run must log
     `Loaded Qt translation "qt_de.qm" from "<extract>/translations"` (use `timeout 5` and temporary
     XDG directories).
   - Record the system libraries the bundle still takes from the host, including
     `libmd4c.so.0`, `libsecret-1.so.0`, `libglib-2.0.so.0`, the X11/XCB stack, the X11/Wayland
     driver libraries, `libjpeg` for the JPEG image plugin, and the OpenSSL/TLS runtime
     (`libssl3`/`libcrypto3`, `ca-certificates`). Do not claim the archive is self-contained.

7. Record the trixie artifacts' failure on Astra before installing the Astra package.
   - Make sure no `qtranslate` is running. Run
     `sudo dpkg -i build/artifacts-linux/qtranslate_0.11.0_amd64.deb` and capture the exact
     unsatisfied-dependency output (expected `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)`; the
     partial configure fails). Then `sudo dpkg --purge qtranslate`, confirm `dpkg -s qtranslate`
     reports not installed and `/usr/bin/qtranslate` is absent. Do not edit `Depends`.
   - Extract the trixie tar.gz to a temporary directory and run `bin/qtranslate --version` with
     `LD_LIBRARY_PATH` unset; capture the exact loader lines. Measured with the current archive:
     `/lib/x86_64-linux-gnu/libc.so.6: version 'GLIBC_2.38' not found` required by
     `lib/libQt6Network.so.6` and `lib/libQt6Gui.so.6`, plus
     `libstdc++.so.6: version 'CXXABI_1.3.15' not found` required by `lib/libQt6Core.so.6` and
     `lib/libicui18n.so.76`.
   - Run the trixie AppImage with `--appimage-extract` (FUSE is not needed) and then
     `squashfs-root/AppRun --version`; capture the exact loader lines: measured `GLIBC_2.38` from
     `libc.so.6` for `libglib-2.0`, `libQt6Network`, `libQt6Gui`, and the other bundled libraries,
     plus child 04's `libsystemd.so.0` `GLIBC_2.39` fact. Record that neither artifact runs on
     Astra and never claim otherwise.

8. Install, launch, and remove the Astra `.deb` on the real host.
   - Preconditions: `pgrep -a qtranslate` is empty; `dpkg -s qtranslate` is not installed; the
     trixie `.deb` from step 7 was purged. Install with
     `sudo dpkg -i build/artifacts-linux/qtranslate_0.11.0+astra1_amd64.deb` (the dependency set is
     satisfied by the host: Qt 6.10.2, `libglib2.0-0` 2.74.6, `libxtst6` 2:1.2.3, `libsecret-1-0`
     0.20.5, glibc 2.36). Record the install output, `dpkg -L qtranslate`, and
     `desktop-file-validate /usr/share/applications/qtranslate.desktop`.
   - Headless version check: with temporary `HOME`/`XDG_CONFIG_HOME`/`XDG_STATE_HOME`/`XDG_RUNTIME_DIR`,
     `xvfb-run -a qtranslate --version` prints `QTranslate 0.11.0`.
   - GUI check under the real `:0`: with the temporary XDG directories and `DISPLAY=:0`, start the
     installed application through the desktop entry (`gtk-launch qtranslate`) so the installed
     `.desktop` file is exercised; if `gtk-launch` cannot start it, run `/usr/bin/qtranslate`
     directly and record why. Poll `xdotool search --name QTranslate` (or `xwininfo -root -tree`)
     until the main window is mapped, and record the window title. Stop the process afterwards
     (`kill -TERM`, wait) and confirm no `qtranslate` process or window is left. The real `:0` is
     the user's session: do not leave windows, do not change the user's settings, and use only the
     temporary XDG directories. If the session is unavailable or the window cannot be observed,
     record the exact reason and keep the `xvfb-run` result as the separate gate, never as a pass.
   - Autostart and XDG paths: with the temporary `XDG_CONFIG_HOME`, write
     `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` with `[app] autostart=true` (and
     `startMinimized=true`), run the installed binary under `xvfb-run` with `--verbose`
     (`timeout 10`), and check `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` contains
     `Exec="/usr/bin/qtranslate" --minimized`, `Icon=qtranslate`, `Type=Application`, and passes
     `desktop-file-validate`. Change the setting to `autostart=false`, rerun, and confirm the file
     is removed. Check the settings file at `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` and the
     `--verbose` log at `$XDG_STATE_HOME/qtranslate/qtranslate.log`.
   - Remove with `sudo dpkg -r qtranslate`; confirm `dpkg -s qtranslate` reports not installed and
     `/usr/bin/qtranslate`, the desktop file, and the icon are gone. Do not leave the package
     installed.

9. Regression, negative paths, and the report.
   - Astra regression: CTest 28/28 in `build/linux-release` and `build/linux-debug` with 0 warnings
     (run `sudo /usr/bin/fly-set-execid.sh` first only if anything was rebuilt). No C++ file changes
     in this task.
   - Trixie regression of the shared script: in the existing `qtranslate-trixie:latest` image (run
     as the host UID with `-e HOME=/tmp` and the repository mounted), configure a Release build in
     `build/trixie-release` (or `/tmp`), build, and run
     `./packaging/linux/package.sh --build-dir <build> --output-dir build/artifacts-linux-trixie-check`
     (the AppImage tools are already cached in `build/linux-tools/appimage`; if the network is
     unavailable, use `--skip-appimage` and record it). Verify the trixie names
     (`qtranslate-<version>-linux-x86_64.tar.gz`, `qtranslate_<version>_amd64.deb`,
     `qtranslate-<version>-x86_64.AppImage` when built), a three-line `SHA256SUMS-linux.txt`, ICU
     76 in `lib/`, the three `imageformats` plugins, and a quick `--version`/`ldd` check. Do not
     copy this check output over `build/artifacts-linux`. If Docker or the image is unavailable,
     record it as not performed. Delete the check output afterwards.
   - Negative paths: `--help` exits 0 and lists `--target` and the Astra names; `--target bogus` is
     a usage error; in the trixie container `--target astra` exits non-zero with the Astra-host
     message before writing anything; on Astra `--target trixie` prints the docker command and the
     new Astra hint and creates nothing; `--target astra --skip-appimage` is accepted; a reduced
     `PATH` names `patchelf`.
   - `git diff --check` is clean; only `packaging/linux/package.sh` changed; no documentation,
     journal, or map file is edited; `build/` stays untracked; do not commit, push, tag, or publish.
   - Report to the lead: the exact Astra artifact names, sizes, and SHA256; the `Depends` line and
     `Version`; the `SHA256SUMS-linux-astra.txt` content; the preserved trixie `SHA256SUMS-linux.txt`
     hash and artifact hashes; the `patchelf` version installed; the trixie failure evidence
     (dependency and loader lines); the tar.gz checks; the `.deb` install/launch/autostart/XDG
     evidence; the not-performed checks (Windows 23 tests, and the real `:0` check if it could not
     run); the trixie regression result; and the decisions in `## Decisions`.

## Decisions

- The Astra mode is explicit (`--target astra`), not auto-detected from `ID=astra`: an implicit
  switch would silently change artifact names on an Astra host and could hide a trixie/container
  run. `--target astra` implies no AppImage; `--skip-appimage` stays accepted.
- The Astra package revision is `+astra1` (Debian revision syntax) only in `Version` and the file
  name; the application version stays `0.11.0` and `--version` still accepts only `X.Y.Z`.
- The Astra target writes `SHA256SUMS-linux-astra.txt` in the same output directory; the trixie
  `SHA256SUMS-linux.txt` and artifacts are never rewritten.
- The bundle content is shared, not forked: the ICU major is derived from the Qt libraries'
  `NEEDED` entries, the image-format plugin set is the measured trixie three, and the Wayland
  plugin is not bundled on Astra (Qt 6.10.2 names it `libqwayland.so`); XWayland is the recorded
  fallback.
- `patchelf` 0.14.3-1+b1 is installed on the Astra host for the packaging run; no other host package
  is added, and the AppImage toolchain is not used there.

## Done when

- `packaging/linux/package.sh --target astra` (documented in `--help`) runs on Astra Linux 1.8.7
  and produces `build/artifacts-linux/qtranslate-0.11.0-linux-x86_64-astra.tar.gz` and
  `build/artifacts-linux/qtranslate_0.11.0+astra1_amd64.deb`; the tar.gz has the one top-level
  directory `qtranslate-0.11.0-linux-x86_64-astra/`, Astra's Qt 6.10.2 libraries with ICU 72, the
  plugins, the 13 Qt translations, `bin/qt.conf`, the desktop file, and the icon; the `.deb` has
  `Version: 0.11.0+astra1`, exactly the three installed files, and `Depends` exactly the Astra
  `dpkg-shlibdeps` output with `libglib2.0-0` and `libqt6core6t64` and without
  `libglib2.0-0t64`/`libxtst6 (>= 2:1.2.5)`; `dpkg-deb --info/--contents/--ctrl-tarfile` show only
  the expected contents and `./control`.
- The `--target astra` run never builds or requires the AppImage (no `curl`, no tool cache) and
  prints `appimage: skipped (Astra target)`; the `--target trixie` gate still refuses Astra with the
  docker command and adds the `--target astra` hint; `--target astra` on a non-Astra host fails
  before writing anything; `--help` lists `--target`; `--target bogus` is a usage error.
- The Astra tar.gz extracted with `LD_LIBRARY_PATH` unset and temporary `HOME`/XDG directories runs
  `bin/qtranslate --version` and prints `QTranslate 0.11.0`; `ldd` has no `not found` and resolves
  Qt/ICU inside the extract; `patchelf --print-rpath` is `$ORIGIN` for `lib/` and `$ORIGIN/../../lib`
  for the plugins; `QT_DEBUG_PLUGINS=1` loads the offscreen plugin from the extract; the
  `uiLanguage=de` + `--verbose` run logs the Qt translation from `<extract>/translations`;
  `desktop-file-validate` passes; `sha256sum -c SHA256SUMS-linux-astra.txt` passes from the output
  directory.
- `SHA256SUMS-linux-astra.txt` lists exactly the two Astra artifacts; `SHA256SUMS-linux.txt` and the
  three trixie artifacts are byte-identical before and after the Astra run (the recorded hashes
  match), and `sentinel.txt` survives; the only new directory is `.qtranslate-staging`, the only new
  files are the two Astra artifacts and `SHA256SUMS-linux-astra.txt`, and nothing is written outside
  `build/`.
- `sudo dpkg -i` installs the Astra `.deb`; `dpkg -L qtranslate` lists `/usr/bin/qtranslate`, the
  desktop file, and the hicolor icon; `desktop-file-validate` passes; `xvfb-run -a qtranslate
  --version` prints `QTranslate 0.11.0`; the GUI starts under the real `:0` (window observed, no
  process or window left behind); `[app] autostart=true` writes
  `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` with `Exec="/usr/bin/qtranslate" --minimized` and
  `autostart=false` removes it; the settings file and the `--verbose` log are under
  `$XDG_CONFIG_HOME/qtranslate/` and `$XDG_STATE_HOME/qtranslate/`; `sudo dpkg -r qtranslate`
  removes the package and `dpkg -s qtranslate` reports not installed.
- The trixie `.deb` failure on Astra is recorded with the exact unsatisfied dependencies
  (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), the trixie tar.gz with the exact `GLIBC_2.38` and
  `CXXABI_1.3.15` loader lines, and the trixie AppImage with the exact `GLIBC_2.38`/`libsystemd`
  `GLIBC_2.39` lines; no trixie artifact is claimed to run on Astra and `Depends` is not edited.
- Astra Debug and Release CTest is 28/28 with 0 warnings (after `fly-set-execid.sh` if anything was
  rebuilt); the trixie packaging regression in `qtranslate-trixie` still produces the trixie names
  and the three-line `SHA256SUMS-linux.txt` (a two-line file is acceptable only with an explicitly
  recorded `--skip-appimage`; otherwise the check is recorded as not performed); Windows Debug/Release
  and the 23 tests are recorded as not performed; only `packaging/linux/package.sh` changed; no
  documentation is edited; the artifacts are not published, committed, tagged, or pushed.

## Risks

- The trixie artifacts are not Astra-compatible by design: the `.deb` names `libglib2.0-0t64` and
  `libxtst6 (>= 2:1.2.5)`; the tar.gz/AppImage bundle Qt 6.8.2, whose `libQt6Gui`/`libQt6Network`
  need `GLIBC_2.38` (and `libstdc++` `CXXABI_1.3.15`), and the AppImage also carries
  `libsystemd.so.0` `GLIBC_2.39`; Astra has glibc 2.36. Record the exact lines; never report a
  trixie artifact as working on Astra and never edit `Depends` to make it pass.
- `/etc/os-release` on Astra reports `VERSION_ID=1.8_x86-64`, not `1.8.7`; gate the Astra mode on
  `ID=astra` only and print the detected values. A version comparison would reject the real host.
- `patchelf` is absent on Astra (measured; candidate 0.14.3-1+b1). Install it with `sudo apt-get
  install -y patchelf` and record the version; the rest of the tool gate (`dpkg-shlibdeps` 1.21.22,
  `dpkg-deb`, `desktop-file-validate`, `readelf`, `ldd`, `du`, `qtpaths6`) is present. Do not add
  `fakeroot`/`lintian` or any other package.
- The hardcoded ICU 76 is wrong on Astra (ICU 72). Derive the major from `libQt6Core.so.6`'s
  `NEEDED` entry; a hardcoded `76` fails `copy_lib` with "library libicui18n.so.76 not found".
  Keep `libicudata` in the list: it is a transitive dependency and not directly `NEEDED` by Qt.
- Astra's `imageformats` has 11 plugins, not 3: `libqpdf.so` needs `libQt6Pdf.so.6` and `libqsvg.so`
  needs `libQt6Svg.so.6`, which are not bundled; `check_ldd` rejects a `libQt6*` resolved outside
  the bundle. Copy the explicit `libqgif.so libqico.so libqjpeg.so` set on both targets; do not
  blanket-copy the family and do not add `libQt6Pdf`/`libQt6Svg` for a text-only application.
- Qt 6.10.2's Wayland platform plugin is `platforms/libqwayland.so`, not
  `libqwayland-generic.so`/`libqwayland-egl.so`; the existing check does not match, so the Astra
  archive gets the XWayland warning and no Wayland plugin. Record the fallback; X11 is the Astra
  target. Do not add a second Wayland code path in this task.
- `libmd4c.so.0` is a `NEEDED` dependency of `libQt6Gui.so.6` on both trixie and Astra but is not
  bundled. On a clean Astra it comes from `libmd4c0` 0.4.8; on this dev host the loader picks the
  local `/usr/local/lib` 0.5.2 build. Record it as a system requirement; do not claim the archive
  is self-contained beyond its glibc baseline.
- The Astra `.deb` `Depends` must be exactly `dpkg-shlibdeps`' output: `libglib2.0-0` 2.74.6 (no
  `t64`), `libxtst6` 2:1.2.3 (no version constraint), `libqt6core6t64 (>= 6.10.2)`, `libsecret-1-0`
  because the binary links `libsecret-1.so.0`, and `libc6 (>= 2.34)`. Do not add or remove entries
  by hand; assert the absence of the trixie pair and the presence of `libglib2.0-0 (` with a regex
  that cannot match `libglib2.0-0t64`.
- The `+astra1` suffix is the Debian revision marker for a separately built package: it belongs only
  to the `.deb` `Version` and the file name. Do not change `AppInfo::version()`/`CMakeLists.txt`,
  do not pass `+astra1` to `--version`, and do not rename the application.
- In the shared output directory, the trixie AppImage is legitimate and is listed in
  `SHA256SUMS-linux.txt`; the Astra run's foreign-AppImage warning would be misleading. Suppress
  that warning in Astra mode; never rewrite the trixie checksum file or delete the trixie artifacts.
  The only path the script removes is `<output-dir>/.qtranslate-staging`.
- `dpkg -i` of the trixie `.deb` leaves the package unpacked but not configured; purge it
  (`sudo dpkg --purge qtranslate`) and verify `/usr/bin/qtranslate` is gone before installing the
  Astra `.deb`. Do not install either package while another `qtranslate` is running. The final
  state must have no `qtranslate` package installed. `sudo` is available without a password on this
  host; if it stops working, record the exact commands and mark the install checks as not performed,
  never as passed.
- The real `:0` check is the user's session: use temporary `HOME`/`XDG_CONFIG_HOME`/`XDG_STATE_HOME`/
  `XDG_RUNTIME_DIR`, do not leave a `qtranslate` process or window, and record the exact reason if
  the window cannot be observed instead of claiming a GUI pass. `wmctrl` is not installed on Astra,
  but `xdotool`, `xwininfo`, `gtk-launch`, and `kate` are.
- The trixie regression of the shared `package.sh` needs the `qtranslate-trixie:latest` image
  (present) and the cached AppImage tools (present). Run it as the host UID so no root-owned files
  appear in `build/`; if Docker or the network is unavailable, use `--skip-appimage` or record the
  check as not performed.
- `SHA256SUMS-linux-astra.txt` must stay distinct from the trixie `SHA256SUMS-linux.txt` and from
  the Windows `SHA256SUMS.txt`; the release job downloads both sets into one directory. The Astra
  artifacts are local verification artifacts: CI publishes only the trixie set (child 06), and the
  user/lead decides whether to attach the Astra files to a release manually.
- Do not commit build artifacts; `build/` stays untracked. Report sizes and checksums; the lead
  updates the docs and release notes. The task changes no C++ code, so Windows/MSVC is diff-only and
  the 23 Windows tests are recorded as not performed.

## Outcome

`packaging/linux/package.sh` now has an explicit `--target trixie|astra` (default `trixie`); nothing
is auto-detected. The Astra target accepts only an `ID=astra` host (the real `/etc/os-release`
reports `VERSION_ID=1.8_x86-64`, so no version comparison), skips the AppImage without requiring
`curl` or the tool cache, and produces `qtranslate-0.11.0-linux-x86_64-astra.tar.gz`,
`qtranslate_0.11.0+astra1_amd64.deb`, and `SHA256SUMS-linux-astra.txt`. The trixie target keeps its
Debian 13 gate, artifact names, docker command, three-line checksum file, and AppImage path; on an
Astra host it adds the `--target astra` hint after the docker command. `--version` stays `X.Y.Z`;
`+astra1` is generated for the Debian version and the file name only.

The bundle logic is shared, not forked: the ICU major comes from `libQt6Core.so.6`'s `NEEDED` entry
(76 on trixie, 72 on Astra), `imageformats` is the explicit `libqgif.so libqico.so libqjpeg.so` set
on both targets, and the Wayland set stays conditional on `libqwayland-generic.so`. Qt 6.10.2 has
no such plugin, so the Astra archive has no Wayland plugin and records the XWayland fallback.
`libmd4c.so.0` stays a system library from `libmd4c0`; the archive is not self-contained beyond its
glibc baseline.

### Host tool

`patchelf` was absent. `sudo apt-get install -y patchelf` could not reach
`releases.devos.astralinux.ru` (NXDOMAIN from the local resolver and from 8.8.8.8; the proxy broke
TLS), so the identical candidate package `patchelf_0.14.3-1+b1_amd64.deb` (SHA256
`0364b90e81faabbb9569076063952c7bcbf07f83d954a2f2602a163d65be60e6`) was fetched from the Debian
pool and installed with `sudo dpkg -i`; `dpkg-query -W patchelf` reports `0.14.3-1+b1`. Nothing else
was installed. The provenance of that `.deb` was not independently verified against the Astra
repository, which is a host-level residual, not a repository change.

### Artifacts

| File | Bytes | SHA256 |
| --- | ---: | --- |
| `qtranslate-0.11.0-linux-x86_64-astra.tar.gz` | 27 289 681 | `f8e5d7e42a5eb13008cf66caa1cce286ec6242619c9cba0cc42140a6bf77e80d` |
| `qtranslate_0.11.0+astra1_amd64.deb` | 403 056 | `6c2f24bbe00f5cea5c57fda5c529808d7b1fd93fabbb234283ea97635b9f797f` |
| `SHA256SUMS-linux-astra.txt` | 211 | `c6af84d9a9a8480a109a1b18fa80a7c4278c38a9ad060b6c5035356fe0a11814` |

`SHA256SUMS-linux-astra.txt` has exactly the two Astra lines, and `sha256sum -c` passes from
`build/artifacts-linux`. The three trixie artifacts and `SHA256SUMS-linux.txt` stayed byte-identical
(`bad8a16f…38f3`, `677be95a…2e82`, `e46dd917…1e45`, `b6a9435f…b839`), `sentinel.txt` survived, and
no other new files appeared; the only new directory state is the regenerated `.qtranslate-staging`.

The tar.gz has one top-level `qtranslate-0.11.0-linux-x86_64-astra/` with Astra's Qt 6.10.2
libraries (14, with SONAME links), ICU 72.1, three platform and three image-format plugins, two TLS
backends, 13 `qt_*.qm` plus 26 dependency catalogs, `bin/qt.conf` (`Prefix = ..`,
`Plugins = plugins`, `Translations = translations`, no host path), the desktop file, and the icon.
Extracted with `LD_LIBRARY_PATH` unset and temporary `HOME`/XDG directories:
`bin/qtranslate --version` prints `QTranslate 0.11.0`; `ldd` on the binary, every `lib/` file, and
every plugin has no `not found` and resolves Qt/ICU inside the extract; the libraries keep
`$ORIGIN`, the plugins `$ORIGIN/../../lib`, and the binary `$ORIGIN:$ORIGIN/../lib`;
`QT_DEBUG_PLUGINS=1` loads `<extract>/plugins/platforms/libqoffscreen.so`; the `uiLanguage=de` +
`--verbose` run logs `Loaded Qt translation "qt_de.qm" from "<extract>/translations"`;
`desktop-file-validate` passes. Host libraries still taken from the system include `libmd4c.so.0`
(here `/usr/local/lib` 0.5.2; `libmd4c0` 0.4.8 is installed), `libsecret-1.so.0`,
`libglib-2.0.so.0`, the X11/XCB stack, `libjpeg.so.62`, EGL/GLX, and the OpenSSL/TLS runtime.

The `.deb` has `Version: 0.11.0+astra1`, `Architecture: amd64`, `Installed-Size: 1396`, exactly the
three installed files, and only `./control` in the control archive. Its exact `Depends` is:

```
libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0 (>= 2.12.0), libqt6core6t64 (>= 6.10.2), libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2), libqt6network6 (>= 6.7.2), libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5), libx11-6, libxtst6
```

There is no `libglib2.0-0t64` and no `libxtst6 (>= ...)`; the Astra mode asserts these names and the
presence of `libglib2.0-0 (`/`libqt6core6t64`, while the trixie `Depends` is untouched.

### Install and launch on Astra

`sudo dpkg -i` installs the package; `dpkg -L` lists `/usr/bin/qtranslate`, the desktop file, and the
256x256 icon; `desktop-file-validate` passes; `xvfb-run -a qtranslate --version` prints
`QTranslate 0.11.0`. Under the real `:0`, launched through the desktop entry with `gtk-launch`, the
window `QTranslate` (`WM_CLASS "qtranslate", "QTranslate"`, 1640x880) was observed; after
`kill -TERM` no process or window remained and the user's settings were untouched. With a temporary
`XDG_CONFIG_HOME` and `[app] autostart=true`, the app writes
`$XDG_CONFIG_HOME/autostart/qtranslate.desktop` with `Exec="/usr/bin/qtranslate" --minimized`,
`Icon=qtranslate`, `Type=Application`, and the file passes `desktop-file-validate`; `autostart=false`
removes it. Settings are at `$XDG_CONFIG_HOME/qtranslate/qtranslate.ini` and the `--verbose` log at
`$XDG_STATE_HOME/qtranslate/qtranslate.log`. `sudo dpkg -r` removes the package; `dpkg -s` reports
not installed and the binary, desktop file, and icon are gone. The real `:0` check succeeded, so it
is not replaced by `xvfb-run`.

### Trixie artifacts on Astra

The trixie `.deb` fails with the exact unsatisfied dependencies `libglib2.0-0t64 (>= 2.12.0)` (not
installed) and `libxtst6 (>= 2:1.2.5)` (the host has `2:1.2.3-1.1+b5`), leaves the package unpacked
and not configured, and is purged before the Astra package; `Depends` was not edited. The trixie
tar.gz fails with `GLIBC_2.38` (from `libQt6Network`, `libQt6Gui`, `libicui18n.so.76`,
`libicuuc.so.76`) and `CXXABI_1.3.15` (from `libQt6Core`, `libicui18n.so.76`). The trixie AppImage,
after `--appimage-extract`, fails with `GLIBC_2.38` (glib, Qt, ICU, and others) and `GLIBC_2.39` for
`libsystemd.so.0`. No trixie artifact is claimed to run on Astra.

### Regression and negative paths

Astra Debug and Release CTest is 28/28 in both trees with no warnings and no rebuild; the reviewer
re-ran both suites with the same result. The trixie packaging regression ran in
`qtranslate-trixie:latest` as the host UID with `HOME=/tmp`: it produced the trixie names, a
three-line `SHA256SUMS-linux.txt`, ICU 76, 21 libraries, 17 plugins (including the three
`imageformats` files), and a Wayland bundle; the extracted trixie bundle prints `QTranslate 0.11.0`
and resolves Qt/ICU inside; the check output was deleted and `build/artifacts-linux` was not touched.
The reviewer re-ran this regression independently with the same result. The review found no remarks.

`--help` exits 0 and lists `--target` and the Astra names; `--target bogus` is a usage error (exit
2); `--target trixie` on Astra exits 1 with the docker command and the `--target astra` hint without
creating anything; `--target astra` in the trixie container exits 1 before writing; a reduced `PATH`
names `patchelf` among the missing tools.

### Not performed and known limits

Windows Debug/Release and the 23 Windows tests are not performed (the task changes no C++ and runs
on Linux only). Wayland sessions on Astra fall back to XWayland; the Astra archives are local
verification artifacts, not published; `build/` stays untracked and nothing was committed, tagged,
or pushed by the programmer. `package.sh` is the only changed code file.

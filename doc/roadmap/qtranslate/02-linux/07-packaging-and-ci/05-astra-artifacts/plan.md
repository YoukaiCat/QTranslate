---
kind: task
status: todo
depends: [02-portable-tarball, 03-deb-package]
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

## Done when

- `packaging/linux/package.sh --target astra` (or the auto-detected Astra mode) produces
  `qtranslate-<version>-linux-x86_64-astra.tar.gz` and
  `qtranslate_<version>+astra1_amd64.deb`; the tar.gz has the same required layout as the trixie
  archive with Astra's Qt 6.10.2, and the `.deb` `Version` is `<version>+astra1` with `Depends` from
  Astra's `dpkg-shlibdeps`.
- The Astra tar.gz runs with `LD_LIBRARY_PATH` unset and `ldd` resolves Qt to the bundle; the Astra
  `.deb` installs with `dpkg -i`, `qtranslate --version` and the GUI launch, the desktop entry,
  icon, autostart, settings, and log checks pass, and `dpkg -r` removes it. If `sudo` is
  unavailable, the exact commands and the reason are recorded as not performed.
- The trixie `.deb`/tar.gz/AppImage failures on Astra are recorded with the exact dependency and
  loader errors; no trixie artifact is claimed to run on Astra.
- `SHA256SUMS-linux-astra.txt` lists both Astra artifacts and `sha256sum -c` passes; the trixie
  `SHA256SUMS-linux.txt` is not overwritten.
- Astra builds 28/28 in Debug and Release with 0 warnings after `sudo /usr/bin/fly-set-execid.sh`;
  Windows 23 tests are recorded as not performed; no documentation is edited; the artifacts are
  not published.

## Risks

- Astra's `dpkg-shlibdeps` may emit version constraints from Astra's own symbols; record the exact
  line instead of copying the trixie one. The Astra `.deb` must not contain
  `libglib2.0-0t64` or `libxtst6 (>= 2:1.2.5)`.
- The `+astra1` suffix is the Debian revision marker for a separately built package. Do not change
  the application version (`AppInfo::version()`/`CMakeLists.txt`); only the package `Version` and
  the artifact file name carry the suffix. If the user prefers another naming convention, the lead
  can rename the artifact without rebuilding.
- The Astra tar.gz bundles Qt 6.10.2; its glibc requirement is Astra's 2.36, so it also runs on
  trixie, but the trixie artifact is the primary one there. Do not present the Astra archive as a
  replacement for the trixie archive in CI.
- The Astra build needs the trusted-client attribute for the X11 test binaries after every
  rebuild. `sudo /usr/bin/fly-set-execid.sh` is required; if it is not run, the two X11 tests
  `QSKIP` and the gate is not meaningful.
- `dpkg -i` requires root and must not run while another `qtranslate` is running; use a temporary
  `HOME`/`XDG_CONFIG_HOME` for the launch checks and remove the package afterwards. The real
  `:0` session may be busy; if the GUI check cannot run, record the exact reason and the
  `xvfb-run` result separately.
- The Astra `.deb` is not published by CI (the runner is Ubuntu; there is no Astra runner). The
  outcome must state this and the user/lead must decide whether to attach the Astra artifacts to a
  release manually.
- Do not commit build artifacts; `build/` stays untracked. Report sizes and checksums; the lead
  updates the docs and release notes.
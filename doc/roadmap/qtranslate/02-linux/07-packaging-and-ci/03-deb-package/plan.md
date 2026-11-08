---
kind: task
status: done
detailed: true
depends: [01-desktop-entry-and-install, 02-portable-tarball]
---

# Debian package

## Task

Extend `packaging/linux/package.sh` so a Trixie run also produces
`qtranslate_<version>_amd64.deb` from the same install staging that the portable archive uses. The
`.deb` is the primary Debian 13 artifact: `usr/bin/qtranslate`, the desktop file, and the hicolor
icon; its `Depends` comes from `dpkg-shlibdeps` against trixie, not from a hand-written list.

Measured facts to use as the gate: the trixie-built executable needs `GLIBC_2.34` and
`GLIBCXX_3.4.14`; `dpkg-shlibdeps` in trixie produces
`libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0t64 (>= 2.12.0), libqt6core6t64 (>= 6.8.2),
libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2), libqt6network6 (>= 6.7.2),
libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5), libx11-6,
libxtst6 (>= 2:1.2.5)` for the current binary. Astra Linux 1.8.7 has glibc 2.36,
`libglib2.0-0` 2.74.6, and `libxtst6` 2:1.2.3, so this trixie `.deb` cannot install there; that is
expected and is covered by the native Astra build in child 05, not by hand-editing `Depends`.

Contract:

- After the tar.gz is assembled, stage the `.deb` root from the same `cmake --install` tree:

  ```
  usr/bin/qtranslate
  usr/share/applications/qtranslate.desktop
  usr/share/icons/hicolor/256x256/apps/qtranslate.png
  DEBIAN/control
  ```

  Do not add a `postinst`/`prerm`, an autostart file, a system-wide `qtranslate.ini`, or a menu
  entry. Autostart and settings stay per-user and are created by the application (task 05).
- `DEBIAN/control` fields: `Package: qtranslate`, `Version: <version>`, `Architecture: amd64`,
  `Maintainer: Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>`,
  `Section: utils`, `Priority: optional`,
  `Homepage: https://github.com/youkaicat/QTranslate`, `Installed-Size: <KiB from du -sk>`, and a
  multi-line English `Description: QTranslate - fast desktop translator` with a short extended
  description. `Depends` is inserted from `dpkg-shlibdeps`.
- Run `dpkg-shlibdeps -O -e<stage>/usr/bin/qtranslate` from a temporary directory that contains a
  minimal `debian/control` source stanza (`Source: qtranslate`, `Section: utils`,
  `Priority: optional`, `Maintainer: ...`, an empty line, `Package: qtranslate`,
  `Architecture: any`, `Description: ...`). Strip the `shlibs:Depends=` prefix and put the result
  in `Depends`. Do not add `libsecret-1-0` by hand: it must appear only because the binary links
  `libsecret-1.so.0`, and `libqt6dbus6` because the binary links QtDBus for the portal. Run
  `dpkg-shlibdeps` on the staged binary, not on the build-tree binary; the "binaries to analyze
  should already be installed in their package's directory" warning is harmless, but the tool
  must find the trixie runtime packages and `dpkg-dev`.
- Add `dpkg-dev` explicitly to `packaging/linux/trixie.Dockerfile` (the image's `build-essential`
  already pulls it in, but the packaging dependency should be visible). The image must have the
  runtime packages whose symbols the binary uses; the current image does.
- Build with `dpkg-deb --build --root-owner-group <stage> <output>/qtranslate_<version>_amd64.deb`
  (no `fakeroot`); the package name, version, and architecture must match the artifact name. Keep
  the Release binary as built; do not strip it only for the `.deb` while the tar.gz keeps the
  unstripped copy.
- Update `SHA256SUMS-linux.txt` in the output directory to contain both the tar.gz and the `.deb`
  lines. Do not create the Windows `SHA256SUMS.txt`.
- Record `dpkg-deb --info`, `dpkg-deb --contents`, `dpkg-deb --ctrl-tarfile` (only `./control`),
  and `dpkg-deb --field <deb> Depends` in the task outcome. Install/remove on trixie: `dpkg -i`,
  `dpkg -L qtranslate`,
  `desktop-file-validate /usr/share/applications/qtranslate.desktop`,
  `xvfb-run -a qtranslate --version`, `dpkg -r qtranslate`; run this in the packaging container or
  a throwaway trixie container, not on the development host.
- On Astra, do not try to make the trixie package install. Record the exact `dpkg -i` failure
  (expected: unsatisfied `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)`) and use the native Astra
  `.deb` from child 05 for the Astra installation check.
- If a no-libsecret build is used to verify the optional dependency: build it with the same
  `package.sh`, check `readelf -d` has no `libsecret-1.so.0`, and check `Depends` has no
  `libsecret-1-0`. Do not add a `Recommends`/`Suggests` to hide the difference.

## Steps

1. Add `dpkg-dev` to the trixie packaging image.
   - `packaging/linux/trixie.Dockerfile`: add `dpkg-dev` as an explicit entry with a comment that
     `dpkg-shlibdeps` computes the `.deb` `Depends`. `build-essential` already pulls it in, but the
     packaging tool must be visible. Update the header comment, which currently says a later child
     adds `dpkg-dev`.
   - Rebuild `docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .`; confirm
     `dpkg-deb --version` and `dpkg-shlibdeps --version` (measured 1.22.22 in trixie) and that
     `dpkg-dev` is installed. Do not add `fakeroot` or `lintian`.
   - When child 06 moves the inline package list to `packaging/linux/trixie-packages.txt`, it must
     carry `dpkg-dev` over.

2. Extend `package.sh`'s contract and tool gate.
   - Header comment and `usage()`: the script now produces the portable archive and
     `qtranslate_<version>_amd64.deb`; list both artifacts and add `dpkg-deb`, `dpkg-shlibdeps`,
     and `du` to the required-tools paragraph.
   - `check_tools`: add `dpkg-deb:dpkg`, `dpkg-shlibdeps:dpkg-dev`, and `du:coreutils` to the
     literal list; keep the single missing-tool message that names the trixie package.
   - Add globals `deb_root=`, `deb_artifact=`, `deb_size=`, `deb_sha=`, `deb_depends=`, and
     `deb_installed_size=`. Do not add a new CLI option; `--version` keeps the `X.Y.Z` regex
     (child 05's `+astra1` suffix is out of scope).
   - `main`: call `build_deb` after `pack_tarball` and before `write_checksums`; `print_summary`
     prints both artifacts. Keep the existing function boundaries: this child extends the script,
     it does not restructure it.

3. Stage the `.deb` root from the same install tree.
   - In `build_deb()`: `deb_root="$stage/deb"`; `mkdir -p "$deb_root/usr"`;
     `cp -a "$install_root/." "$deb_root/usr/"`. This yields exactly `usr/bin/qtranslate`,
     `usr/share/applications/qtranslate.desktop`, and
     `usr/share/icons/hicolor/256x256/apps/qtranslate.png`; the install tree stays bundle-free.
   - Assert `-x usr/bin/qtranslate`, regular desktop and icon files, modes `0755`, `0644`, and
     `0644`, no `usr/bin/qt.conf`, no `usr/share/qt6`, and no `usr/lib`, `usr/plugins`, or
     `usr/translations`; print the sorted file list. Do not copy from `bundle_root`: it carries the
     Qt libraries, plugins, translations, and `qt.conf`.
   - `mkdir -p "$deb_root/DEBIAN"` **before** the `dpkg-shlibdeps` call. Measured with dpkg
     1.22.22: with `DEBIAN/` present, `dpkg-shlibdeps` emits no stderr; without it, it prints a
     `$ORIGIN ... lack of DEBIAN sub-directory` warning per NEEDED library plus the `binaries to
     analyze should already be installed` warning. The warnings are cosmetic, but the clean staging
     root is the better gate.
   - Do not strip the binary: the `.deb` ships the same `bin/qtranslate` as the tar.gz. Record
     `sha256sum` of `install_root/bin/qtranslate` and the staged copy and require equality before
     and after `dpkg-deb`; the builder does not modify the source file.

4. Compute `Depends` with `dpkg-shlibdeps`.
   - Create `$stage/dpkg/debian/control` (outside `$deb_root`, so it is never packaged; the
     staging directory is removed on the next run; never create a `debian/` tree in the
     repository) with exactly:

     ```
     Source: qtranslate
     Section: utils
     Priority: optional
     Maintainer: Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>

     Package: qtranslate
     Architecture: any
     Description: QTranslate - fast desktop translator
     ```

   - Run `( cd "$stage/dpkg" && dpkg-shlibdeps -O -e"$deb_root/usr/bin/qtranslate" )`; extract
     only the `shlibs:Depends=` line and strip its prefix; die if the result is empty. Do not append
     `libsecret-1-0`, `libqt6dbus6`, or any other entry by hand.
   - Measured output for the staged 0.11.0 binary:
     `libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0t64 (>= 2.12.0), libqt6core6t64 (>= 6.8.2),
     libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2), libqt6network6 (>= 6.7.2),
     libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5), libx11-6,
     libxtst6 (>= 2:1.2.5)`.
   - Assert that `libc6 (>= 2.34)`, `libqt6core6t64`, `libqt6dbus6`, and `libxtst6` are present.
     Conditional libsecret gate: if `readelf -d "$deb_root/usr/bin/qtranslate"` contains
     `libsecret-1.so.0`, `Depends` must contain `libsecret-1-0`; otherwise it must not.
     `libglib2.0-0t64` is in the list because the libsecret link pulls in glib; do not treat it as
     a hand-written entry.
   - If `dpkg-shlibdeps` fails with missing dependency information, the image lacks the runtime
     package whose shlibs file is needed; install it in the image. Do not fall back to a manual
     list.

5. Write `DEBIAN/control` and build the `.deb`.
   - `deb_installed_size=$(du -sk "$deb_root/usr")`; strip the path; the result must be a positive
     integer. This is `Installed-Size` in KiB (unpacked files only, not the artifact size; the
     `DEBIAN` directory is excluded). Measured on the current tree: about `1360` KiB after `cp -a`;
     the exact value can vary with block allocation, so record the run's value.
   - Write `$deb_root/DEBIAN/control` with exactly the fields from `## Task`, in this order:
     `Package`, `Version`, `Architecture: amd64`, `Maintainer`, `Installed-Size`, `Depends`,
     `Section: utils`, `Priority: optional`, `Homepage`, `Description`. Use an unquoted heredoc for
     `$version`, `$deb_depends`, and `$deb_installed_size`, and **do not escape `@`** in the
     Maintainer: measured, `\@` appears literally in `dpkg-deb --field`. Set mode `0644` and keep
     the trailing newline. The extended description:
     ```
     Description: QTranslate - fast desktop translator
      QTranslate translates text selected in other applications with a global
      hotkey. It supports Google Translate, DeepL, and OpenAI-compatible
      providers, and keeps its settings per user.
     ```
   - `dpkg-deb --build --root-owner-group "$deb_root" "$output_dir/qtranslate_${version}_amd64.deb"`;
     no `fakeroot`; the name must be exactly `qtranslate_<version>_amd64.deb`; record the size.
   - Self-check with `dpkg-deb --field`: `Maintainer` has no backslash and is the exact identity,
     `Version`, `Architecture`, `Section`, `Priority`, and `Homepage` match, `Installed-Size`
     equals the computed value, and `Depends` equals `$deb_depends`. `dpkg-deb --contents` lists
     only the three files plus directories; `dpkg-deb --ctrl-tarfile` lists only `./control`, so the
     package has no `postinst` or `prerm`.

6. Update the checksums and the summary.
   - `write_checksums`: rewrite `SHA256SUMS-linux.txt` with both lines from the output directory:
     `sha256sum "qtranslate-$version-linux-x86_64.tar.gz" "qtranslate_${version}_amd64.deb" >
     SHA256SUMS-linux.txt`; set both `artifact_sha` and `deb_sha`. Never create `SHA256SUMS.txt`.
   - `print_summary`: add `deb: $deb_artifact`, `deb size`, `deb sha256`, `depends`, and
     `installed size`; keep the existing archive fields.
   - The tar is packed before `build_deb`, so `DEBIAN`, `deb/`, and `dpkg/` must not appear in
     `tar -tzf`; assert the archive still has one top-level bundle directory.

7. Verify the positive path on trixie.
   - Rebuild the image (step 1); configure Release in a container as the host UID with
     `--user "$(id -u):$(id -g)"`, `-e HOME=/tmp`, and `-e LANG=C.UTF-8`; build and run
     `./packaging/linux/package.sh --build-dir <build> --output-dir build/artifacts-linux`.
   - Record the exact output of `dpkg-deb --info`, `dpkg-deb --contents`,
     `dpkg-deb --ctrl-tarfile` (list the entries and require only `./control`), and
     `dpkg-deb --field <deb> Depends`, `Installed-Size`, `Maintainer`, `Homepage`, and
     `Description`.
   - Confirm `sha256sum -c SHA256SUMS-linux.txt` passes from the output directory; the sentinel
     file from child 02 survives; the only new entries are the staging directory, both artifacts,
     and the checksum file; nothing is written outside `build/`.
   - Confirm the tar.gz still has the child 02 layout (one top-level directory, no
     `DEBIAN`/`deb`/`dpkg` entries); `desktop-file-validate` on the install tree passes.

8. Install and remove on trixie in a throwaway container.
   - `docker run --rm -v "$PWD/build/artifacts-linux:/artifacts:ro" qtranslate-trixie bash -lc
     '...'` — a disposable instance of the packaging image, which has the trixie runtime packages
     and `desktop-file-utils`. Do not install on the development host or Astra; make sure no
     `qtranslate` is running.
   - Inside: `dpkg -i /artifacts/qtranslate_<version>_amd64.deb`; `dpkg -L qtranslate`;
     `desktop-file-validate /usr/share/applications/qtranslate.desktop`; with temporary
     `HOME`/`XDG_CONFIG_HOME`/`XDG_STATE_HOME`/`XDG_RUNTIME_DIR`,
     `xvfb-run -a qtranslate --version` prints `QTranslate <version>`; `dpkg -r qtranslate`;
     assert `/usr/bin/qtranslate`, the desktop file, and the icon are gone and `dpkg -s qtranslate`
     reports not installed.
   - The log may show `Processing triggers for desktop-file-utils`; that trigger comes from the
     image, not from the package (the package has no maintainer scripts). Record the exact
     install/remove output.

9. Prove the optional libsecret dependency with a no-libsecret build.
   - In a throwaway container, remove the development package (`dpkg -r libsecret-1-dev`; the
     runtime `libsecret-1-0` stays), configure a separate Release build under `/tmp` (for example
     `/tmp/trixie-nosecret`), build it, and assert `readelf -d .../qtranslate` has no
     `libsecret-1.so.0` and `LIBSECRET_FOUND` is not `TRUE` in `CMakeCache.txt`.
   - Run the same `package.sh` with `--build-dir /tmp/trixie-nosecret --output-dir
     /tmp/artifacts-linux-nosecret`; `dpkg-deb --field <deb> Depends` must have no
     `libsecret-1-0`, and the conditional gate in step 4 must pass. Install/remove this `.deb` in
     the throwaway container; it must install on trixie.
   - Do not use `-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON`: measured, it breaks Qt 6.8's
     `FindXKB.cmake` with `Unknown CMake command "pkg_check_modules"`. Do not add
     `Recommends`/`Suggests`; do not ship this build.

10. Record the Astra expectation, run the regression, and report.
    - Astra Linux 1.8.7: run `dpkg -i` on the trixie `.deb` with `sudo` if available and capture
      the exact failure. Expected: unsatisfied `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)`. Do
      not edit `Depends`; never report it as passing. If `sudo` is unavailable, record the exact
      command, the package `Depends`, and `dpkg-query -W libglib2.0-0t64 libglib2.0-0 libxtst6`
      as not performed; child 05 covers the native Astra `.deb`.
    - Trixie regression: Debug and Release configure/build with `-Wall -Wextra`, no warnings, and
      `timeout 300 xvfb-run -a ctest --test-dir <build> --output-on-failure` 28/28 in both. Astra:
      28/28 in the existing build trees, after `sudo /usr/bin/fly-set-execid.sh` if anything was
      rebuilt. Windows Debug/Release and the 23 tests: not performed (no MSVC), reviewed by diff.
    - Negative paths: `--help` lists the `.deb` and the new tools; a reduced `PATH` names
      `dpkg-deb` and `dpkg-shlibdeps` in the missing-tool error; an invalid `--version`, a missing
      `CMakeCache.txt`, and a Debug cache still fail with the existing messages; the Astra host
      gate still prints the docker command before the tool gate.
    - `git diff --check`; only `packaging/linux/package.sh` and `packaging/linux/trixie.Dockerfile`
      changed; no documentation or map files. Report: `Maintainer`, `Homepage`, the exact synopsis
      and extended Description, the `Depends` string, `Installed-Size`, `.deb` size and SHA256,
      the install/remove evidence, the no-libsecret result, the Astra failure, and the
      not-performed checks.

## Done when

- `qtranslate_<version>_amd64.deb` is produced by the same `package.sh` run as the tar.gz and
  contains exactly `usr/bin/qtranslate` (mode `0755`), `usr/share/applications/qtranslate.desktop`
  (mode `0644`), and `usr/share/icons/hicolor/256x256/apps/qtranslate.png` (mode `0644`), all owned
  `root/root` in the archive, plus `DEBIAN/control`; no autostart file, no system config, no
  `postinst`/`prerm`, no menu entry, and no `lib/`, `plugins/`, `translations/`, or `bin/qt.conf`.
- `dpkg-deb --field <deb> Depends` is exactly the `dpkg-shlibdeps -O` result with the
  `shlibs:Depends=` prefix removed and nothing appended. For the current trixie build it contains
  `libc6 (>= 2.34)`, `libqt6core6t64`, `libqt6gui6`, `libqt6widgets6`, `libqt6network6`,
  `libqt6dbus6`, `libsecret-1-0`, `libx11-6`, and `libxtst6 (>= 2:1.2.5)`. A build without
  libsecret has no `libsecret-1-0` entry and still installs on trixie.
- `Maintainer: Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>` and
  `Homepage: https://github.com/youkaicat/QTranslate` are exact (no backslash in the email);
  `Version`, `Architecture: amd64`, `Section: utils`, `Priority: optional`, and the multi-line
  English `Description` from step 5 are present; `Installed-Size` equals `du -sk` of `usr`.
- `dpkg-deb --info`, `dpkg-deb --contents`, and `dpkg-deb --ctrl-tarfile` (only `./control`) are
  recorded; in a throwaway `qtranslate-trixie` container (not the development host or Astra)
  `dpkg -i` succeeds, `dpkg -L qtranslate` lists the three files, `desktop-file-validate` passes on
  `/usr/share/applications/qtranslate.desktop`, `xvfb-run -a qtranslate --version` prints
  `QTranslate <version>`, and `dpkg -r qtranslate` removes the package and its files; no
  `qtranslate` is running during the check.
- The no-libsecret build (a separate Release build in a throwaway container without
  `libsecret-1-dev`) has no `libsecret-1.so.0` in `readelf -d` and no `libsecret-1-0` in
  `dpkg-deb --field <deb> Depends`; its `.deb` installs on trixie; `Recommends`/`Suggests` are not
  used to hide the difference.
- `SHA256SUMS-linux.txt` lists both the tar.gz and the `.deb`, `sha256sum -c` passes, and the
  Windows `SHA256SUMS.txt` name is not created. The tar.gz still contains only the bundle root and
  no `DEBIAN`/`deb`/`dpkg` entries.
- On Astra the trixie `.deb` failure is recorded with the exact unsatisfied dependencies
  (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), never as passed; if `sudo` is unavailable, the
  command and the dependency facts are recorded as not performed; the Astra installation check is
  deferred to child 05 with the native package, and `Depends` is not edited by hand.
- Debian 13 builds Debug/Release with no warnings and CTest 28/28; Astra still builds 28/28 after
  `fly-set-execid.sh` if anything was rebuilt; Windows 23 tests are recorded as not performed; only
  `packaging/linux/package.sh` and `packaging/linux/trixie.Dockerfile` change; no documentation is
  edited.

## Risks

- `dpkg-shlibdeps` needs a `debian/control` file in the working directory; without it, it warns
  about unknown packages or fails. Keep the temporary control outside the repository, or create
  and remove it inside the staging directory; never leave a `debian/` tree in the repository.
  With `$deb_root/DEBIAN` present, dpkg 1.22.22 emits neither the `$ORIGIN` RUNPATH warning nor the
  "binaries to analyze" warning (measured); if either appears, check the staging layout first.
- The trixie `Depends` is not Astra-compatible by design: `libglib2.0-0t64` and
  `libxtst6 (>= 2:1.2.5)` do not exist on Astra 1.8.7. Do not relax or hand-edit the line to make
  `dpkg -i` pass; that would hide a real dependency difference and contradicts the separate
  Astra build decision.
- `libsecret` is optional in the build. On trixie the normal build links it, so the `.deb` must
  carry `libsecret-1-0`; a no-libsecret build must not. Check `readelf -d`/`ldd` and the generated
  `Depends`, not assumptions. Do not use `-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON` for the
  no-libsecret check: it breaks Qt 6.8's `FindXKB.cmake` (measured). Remove `libsecret-1-dev` in a
  throwaway container instead.
- `dpkg-deb --build --root-owner-group` requires dpkg 1.19+ (trixie has 1.22); do not add
  `fakeroot`. Ensure the binary is `0755`, the desktop file `0644`, and the icon `0644`; `dpkg-deb`
  preserves the staging permissions. The `cmake --install` tree should already carry these modes;
  assert them instead of applying a blanket `chmod`.
- The `Maintainer` email must be written literally. Measured: escaping `@` as `\@` in the control
  heredoc leaves a backslash in `dpkg-deb --field Maintainer`. Verify the field after building.
- Installing the package inside the build container writes to `/usr`; remove it with
  `dpkg -r qtranslate` in the same disposable container, and do not run it on the Astra desktop for
  the trixie artifact. The package must not be installed while another `qtranslate` is running.
  `Processing triggers for desktop-file-utils` in the log is an image trigger, not a package
  maintainer script; `dpkg-deb --ctrl-tarfile` must list only `./control`.
- `Installed-Size` must be the installed size in KiB, not the artifact size; use `du -sk` on
  `$deb_root/usr` (excluding `DEBIAN`). The value can vary with filesystem block allocation between
  runs; record the run's value in the outcome.
- The `Maintainer` and `Homepage` values are user-facing package metadata. Use the project identity
  above and report them to the lead; do not invent a different maintainer. The exact synopsis and
  extended `Description` are in step 5; report them as written.
- `SHA256SUMS-linux.txt` must list both artifacts and must not collide with the Windows
  `SHA256SUMS.txt` in the release job. Do not reuse the Windows name even temporarily. Child 04
  will append the AppImage line to the same file.
- The package does not install the icon in other hicolor sizes; the single 256x256 file is
  enough, and `Icon=qtranslate` matches it. Do not add a second icon name.
- The `--version` regex stays `X.Y.Z` in this child. Child 05's native Astra package version
  `+astra1` and its artifact naming are a separate change; do not preemptively relax the regex or
  add an Astra mode to this script.
- Child 06 later moves the inline package list to `packaging/linux/trixie-packages.txt`; that list
  must include `dpkg-dev` (for `dpkg-shlibdeps`) alongside the existing `patchelf`, `qt6-wayland`,
  and `qt6-translations-l10n` entries.
- A bare `debian:trixie` container cannot pass `dpkg -i` for this package without first installing
  the runtime dependencies; use a throwaway instance of `qtranslate-trixie` for the install/remove
  gate, or install the dependencies first and record the exact command. Do not claim a bare image
  install as passing. The `.deb` is not distribution-independent; it targets trixie's package
  names.

## Outcome

Done on 2026-11-08. `packaging/linux/package.sh` now builds
`qtranslate_<version>_amd64.deb` in the same run as the portable archive, from the bundle-free
`cmake --install` tree, and `packaging/linux/trixie.Dockerfile` lists `dpkg-dev` explicitly. Only
these two files changed for the task; this outcome and the journal are the lead's documentation.
The trixie image was rebuilt (`qtranslate-trixie:latest`,
`sha256:60ee8a378b70b49bac54fc3e841e13a7d6a1cc1c58967fd53a1da9523e8fe318`); `dpkg-deb`,
`dpkg-shlibdeps` and `dpkg-dev` are 1.22.22, `fakeroot` and `lintian` are not installed.

### Artifact and metadata

- `qtranslate-0.11.0-linux-x86_64.tar.gz`: 28 636 818 bytes, SHA256
  `067e91444dc53b9637bcac4cae7ab167f31ec3f7bc0ee163145bd41986e71e76`.
- `qtranslate_0.11.0_amd64.deb`: **405 860 bytes**, SHA256
  **`1dbfb3c55bb357e946286a3b9f2d41763b5c3d5a0a4f69ed04c13faf5ac1b348`**.
- `Installed-Size: 1392` KiB (`du -sk` of the staged `usr` in this run; the earlier ~1360 estimate
  varies with block allocation, and the self-check compares the run's value).
- `Maintainer: Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>` (no backslash),
  `Homepage: https://github.com/youkaicat/QTranslate`, `Version: 0.11.0`,
  `Architecture: amd64`, `Section: utils`, `Priority: optional`.
- Synopsis `QTranslate - fast desktop translator`; extended description:
  ```
   QTranslate translates text selected in other applications with a global
   hotkey. It supports Google Translate, DeepL, and OpenAI-compatible
   providers, and keeps its settings per user.
  ```
- `Depends` is exactly the `dpkg-shlibdeps -O` output with `shlibs:Depends=` removed and nothing
  appended:
  `libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0t64 (>= 2.12.0), libqt6core6t64 (>= 6.8.2),
  libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2), libqt6network6 (>= 6.7.2),
  libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5), libx11-6,
  libxtst6 (>= 2:1.2.5)`. `libsecret-1-0` and `libqt6dbus6` appear only because the binary links
  them; the conditional gate checks `readelf -d` against the `libsecret-1-0` entry.

`dpkg-deb --info qtranslate_0.11.0_amd64.deb`:
```
 new Debian package, version 2.0.
 size 405860 bytes: control archive=660 bytes.
     752 bytes,    13 lines      control
 Package: qtranslate
 Version: 0.11.0
 Architecture: amd64
 Maintainer: Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>
 Installed-Size: 1392
 Depends: libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0t64 (>= 2.12.0), libqt6core6t64 (>= 6.8.2), libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.1.2), libqt6network6 (>= 6.7.2), libqt6widgets6 (>= 6.3.0), libsecret-1-0 (>= 0.7), libstdc++6 (>= 5), libx11-6, libxtst6 (>= 2:1.2.5)
 Section: utils
 Priority: optional
 Homepage: https://github.com/youkaicat/QTranslate
 Description: QTranslate - fast desktop translator
  QTranslate translates text selected in other applications with a global
  hotkey. It supports Google Translate, DeepL, and OpenAI-compatible
  providers, and keeps its settings per user.
```

`dpkg-deb --contents` has only directories and the three regular files, all `root/root`:
`./usr/bin/qtranslate` (0755, 1 365 328 bytes),
`./usr/share/applications/qtranslate.desktop` (0644, 338 bytes),
`./usr/share/icons/hicolor/256x256/apps/qtranslate.png` (0644, 17 013 bytes). The binary has no
strip: SHA256 `fef3792c0d773403edb526a3ab3f04092b081a7a97a4b840c444a72b60a44973` is identical in
the install tree, the staged copy, the `.deb` member and `bin/qtranslate` inside the tar.gz.

`dpkg-deb --ctrl-tarfile | tar -t` lists `./` and `./control` only, so there is no `postinst` or
`prerm`; `./` is dpkg-deb's standard control-archive root entry (verified with a minimal package
built by the same dpkg 1.22.22), and the script ignores only that entry and requires exactly
`./control`. No autostart file, system config, menu entry, `usr/bin/qt.conf`, `usr/share/qt6`,
`usr/lib`, `usr/plugins`, or `usr/translations` is packaged.

`SHA256SUMS-linux.txt` contains both artifact lines and `sha256sum -c` passes; the Windows
`SHA256SUMS.txt` is not created. The child 02 sentinel file survived the run; the output directory
holds only the staging directory, both artifacts, the checksum file, and the sentinel. The tar.gz
has one top-level directory, `qtranslate-0.11.0-linux-x86_64`, and no `DEBIAN`/`deb`/`dpkg`
entries; the script now asserts both properties in `pack_tarball()`.

### Install and remove on trixie

In a throwaway `qtranslate-trixie` container (not the development host or Astra):
`dpkg -i /artifacts/qtranslate_0.11.0_amd64.deb` succeeded (`Setting up qtranslate` plus the
image's `Processing triggers for desktop-file-utils`), `dpkg -L qtranslate` listed
`/usr/bin/qtranslate`, the desktop file, and the icon, `desktop-file-validate
/usr/share/applications/qtranslate.desktop` passed, `xvfb-run -a qtranslate --version` printed
`QTranslate 0.11.0`, and `dpkg -r qtranslate` removed the three files; `dpkg -s qtranslate`
reports not installed. No `qtranslate` was running.

### No-libsecret build

In a separate throwaway container with `libsecret-1-dev` removed (runtime `libsecret-1-0`
0.21.7-1 stayed), a clean Release build under `/tmp/trixie-nosecret` has `LIBSECRET_FOUND` empty
and no `libsecret-1.so.0` in `readelf -d`; the same `package.sh` produced
`qtranslate_0.11.0_amd64.deb` (404 204 bytes, SHA256
`159bfc4e99d2a34677fd1489cfebc76bb73b825cb8a57a92f6c1fa4ec7693d20`, `Installed-Size` 1356 KiB)
whose `Depends` has neither `libsecret-1-0`
nor `libglib2.0-0t64`; that `.deb` installs and removes on trixie. `-DCMAKE_DISABLE_FIND_PACKAGE_
PkgConfig=ON` was not used, no `Recommends`/`Suggests` was added, and this build is not shipped.
The later tar-layout assertion does not depend on libsecret; the reviewer independently
reproduced the no-libsecret gate on the same code path.

### Astra expectation

The trixie `.deb` does not install on Astra Linux 1.8.7; this is expected and is not a pass.
`sudo dpkg -i build/artifacts-linux/qtranslate_0.11.0_amd64.deb` unpacks the package and then
fails to configure:
```
qtranslate зависит от libglib2.0-0t64 (>= 2.12.0), однако:
 Пакет libglib2.0-0t64 не установлен.
qtranslate зависит от libxtst6 (>= 2:1.2.5), однако:
 Версия libxtst6:amd64 в системе — 2:1.2.3-1.1+b5.
dpkg: проблемы зависимостей — оставляем не настроенным
```
The host was cleaned with `sudo dpkg --purge qtranslate`; `dpkg -s qtranslate` reports not
installed and `/usr/bin/qtranslate` is absent. `Depends` was not edited. The Astra installation
check remains deferred to child 05 with the native Astra package.

### Regression and negative paths

- trixie Debug and Release: `-Wall -Wextra`, no warnings, CTest 28/28 in both.
- Astra: 28/28 in the existing `build/linux-debug` and `build/linux-release` after
  `sudo /usr/bin/fly-set-execid.sh`; the task changed no C++ code.
- Windows Debug/Release and the 23 tests: **not performed** (no MSVC), reviewed by diff.
- `--help` lists the `.deb` and `dpkg-deb`/`dpkg-shlibdeps`/`du`; a reduced `PATH` names
  `dpkg-deb (dpkg)` and `dpkg-shlibdeps (dpkg-dev)`; an invalid `--version`, a missing
  `CMakeCache.txt`, and a Debug cache fail with the existing messages; the Astra host gate still
  prints the docker command before the tool gate.
- `git diff --check` is clean; only the two task files changed; no `debian/` tree was created in
  the repository.

Known environment caveat, unchanged by this task: `ctest` in the `qtranslate-trixie` image was run
as root because the image has no `/etc/passwd` entry for UID 1000, so `dbus-daemon` fails for
`tst_hotkeymanager_wayland`; the packaging run itself used the host UID as the plan requires.

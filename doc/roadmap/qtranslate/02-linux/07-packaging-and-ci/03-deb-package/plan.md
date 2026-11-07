---
kind: task
status: todo
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
- Record `dpkg-deb --info`, `dpkg-deb --contents`, and `dpkg-deb --field qtranslate Depends` in the
  task outcome. Install/remove on trixie: `dpkg -i`, `dpkg -L qtranslate`,
  `desktop-file-validate /usr/share/applications/qtranslate.desktop`,
  `xvfb-run -a qtranslate --version`, `dpkg -r qtranslate`; run this in the packaging container or
  a throwaway trixie container, not on the development host.
- On Astra, do not try to make the trixie package install. Record the exact `dpkg -i` failure
  (expected: unsatisfied `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)`) and use the native Astra
  `.deb` from child 05 for the Astra installation check.
- If a no-libsecret build is used to verify the optional dependency: build it with the same
  `package.sh`, check `readelf -d` has no `libsecret-1.so.0`, and check `Depends` has no
  `libsecret-1-0`. Do not add a `Recommends`/`Suggests` to hide the difference.

## Done when

- `qtranslate_<version>_amd64.deb` is produced by the same `package.sh` run as the tar.gz and
  contains exactly `usr/bin/qtranslate`, `usr/share/applications/qtranslate.desktop`, and
  `usr/share/icons/hicolor/256x256/apps/qtranslate.png` (plus `DEBIAN/control` internally); no
  autostart file, no system config, no postinst/prerm.
- `dpkg-deb --field qtranslate Depends` is the `dpkg-shlibdeps` result; for the current trixie
  build it contains `libqt6core6t64`, `libqt6gui6`, `libqt6widgets6`, `libqt6network6`,
  `libqt6dbus6`, `libsecret-1-0`, `libx11-6`, `libxtst6`, and `libc6 (>= 2.34)`. A build without
  libsecret has no `libsecret-1-0` entry and still installs on trixie.
- `dpkg-deb --info` and `dpkg-deb --contents` are recorded; on trixie `dpkg -i` succeeds,
  `qtranslate --version` prints the version under `xvfb-run`, the desktop file passes
  `desktop-file-validate` from `/usr/share/applications`, and `dpkg -r qtranslate` removes the
  package and its files.
- The output directory contains `SHA256SUMS-linux.txt` with both artifact lines, and
  `sha256sum -c` passes. The Windows `SHA256SUMS.txt` name is not used.
- On Astra the trixie `.deb` failure is recorded with the exact unsatisfied dependencies
  (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), never as passed; the Astra installation check is
  deferred to child 05 with the native package.
- Debian 13 builds Debug/Release with no warnings and CTest 28/28; Astra still builds 28/28 after
  `fly-set-execid.sh`; Windows 23 tests are recorded as not performed; no documentation is edited.

## Risks

- `dpkg-shlibdeps` needs a `debian/control` file in the working directory; without it, it warns
  about unknown packages or fails. Keep the temporary control outside the repository, or create
  and remove it inside the staging directory; never leave a `debian/` tree in the repository.
- The trixie `Depends` is not Astra-compatible by design: `libglib2.0-0t64` and
  `libxtst6 (>= 2:1.2.5)` do not exist on Astra 1.8.7. Do not relax or hand-edit the line to make
  `dpkg -i` pass; that would hide a real dependency difference and contradicts the separate
  Astra build decision.
- `libsecret` is optional in the build. On trixie the normal build links it, so the `.deb` must
  carry `libsecret-1-0`; a no-libsecret build must not. Check `readelf -d`/`ldd` and the generated
  `Depends`, not assumptions.
- `dpkg-deb --build --root-owner-group` requires dpkg 1.19+ (trixie has 1.22); do not add
  `fakeroot`. Ensure the binary is `0755`, the desktop file `0644`, and the icon `0644`; `dpkg-deb`
  preserves the staging permissions.
- Installing the package inside the build container writes to `/usr`; remove it with
  `dpkg -r qtranslate` in the same container, and do not run it on the Astra desktop for the
  trixie artifact. The package must not be installed while another `qtranslate` is running.
- `Installed-Size` must be the installed size in KiB, not the artifact size; use `du -sk` on the
  staged root (excluding `DEBIAN` or including it consistently, as dpkg does).
- The `Maintainer` and `Homepage` values are user-facing package metadata. Use the project identity
  above and report them to the lead; do not invent a different maintainer.
- `SHA256SUMS-linux.txt` must list both artifacts and must not collide with the Windows
  `SHA256SUMS.txt` in the release job. Do not reuse the Windows name even temporarily.
- The package does not install the icon in other hicolor sizes; the single 256x256 file is
  enough, and `Icon=qtranslate` matches it. Do not add a second icon name.
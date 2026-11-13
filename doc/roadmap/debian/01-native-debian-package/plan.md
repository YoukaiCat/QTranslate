---
kind: stage
status: in-progress
---

# Native Debian package

## Goal

Add a top-level `debian/` directory that makes the repository a native Debian source package. On
Astra Linux 1.8.7 the exact command

```
DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc
```

(no trailing `.`) must build the `.deb` with the application binary, the desktop entry, the icon,
the standard Debian documentation, and `Depends` from `dh_shlibdeps`. The manual `.deb` staging in
`packaging/linux/package.sh` is removed in stage 2; this stage does not change the script or the
application code.

## What to do, what is touched

The work is split into three children. Each has its own result and gate; later children extend the
same `debian/` tree and the same native build.

1. [01-debian-skeleton-and-build](01-debian-skeleton-and-build/plan.md) — `debian/control`,
   `debian/rules`, `debian/changelog`, `debian/copyright`, `debian/source/format`,
   `debian/source/options`, `debian/.gitignore`, and `debian/README.Debian`; the exact
   `dpkg-buildpackage` command on a clean copy; package contents, `Depends` from `dh_shlibdeps`,
   install/remove, and the `nocheck`/`nodoc`/`parallel` behavior with the tests under `xvfb-run`.
2. [02-package-content-and-dependencies](02-package-content-and-dependencies/plan.md) — a
   `packaging/linux/verify-deb.sh` gate for the package contents and dependencies:
   `dpkg-deb --contents`/`--info`, `dpkg -I`/`-c`/`-L`, install/remove, `desktop-file-validate`,
   `dpkg-checkbuilddeps`, `apt-get build-dep`, the optional-libsecret rule, and the
   `libglib2.0-0`/`libglib2.0-0t64` host rule.
3. [03-source-package-and-hygiene](03-source-package-and-hygiene/plan.md) — the source package
   format and hygiene: `3.0 (native)`, `debian/source/options` with the `build`, `build-*`, `out`
   exclusions, `debian/changelog`/`copyright`, a clean-copy build, `dpkg-source -b`/`-x`,
   `dpkg-buildpackage -S`/`-b`/`-tc`, `debian/rules clean`, and `debian/.gitignore`.

Application code, tests, `CMakeLists.txt`, and `packaging/linux/package.sh` are not touched in this
stage. The only non-`debian/` file added here is `packaging/linux/verify-deb.sh` in child 02; it is
a verification tool, not a change to the release script.

## Done when

- `debian/` exists with `control`, `rules`, `changelog`, `copyright`, `source/format`,
  `source/options`, `.gitignore`, and `README.Debian`; `debian/rules` is executable;
  `debian/changelog` is `0.12.0` and matches `project(qtranslate VERSION 0.12.0)` in
  `CMakeLists.txt`; `debian/source/format` is `3.0 (native)`.
- On Astra Linux 1.8.7, from a clean copy, `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc"
  dpkg-buildpackage -rfakeroot -us -uc` (no `.`) exits 0 and writes
  `qtranslate_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc`, and `qtranslate_0.12.0.tar.xz` to the
  parent directory.
- `dpkg-deb --contents` and `dpkg-deb --info` show `usr/bin/qtranslate`,
  `usr/share/applications/qtranslate.desktop`,
  `usr/share/icons/hicolor/256x256/apps/qtranslate.png`, and the standard
  `usr/share/doc/qtranslate/copyright` and `changelog.Debian.gz`; no `usr/bin/qt.conf`,
  `usr/share/qt6`, `usr/lib`, `usr/plugins`, or `usr/translations` are present.
- `Depends` comes from `dh_shlibdeps`; on Astra it contains `libglib2.0-0` and not
  `libglib2.0-0t64`, on trixie it contains `libglib2.0-0t64`; `libsecret-1-0` appears only when
  the binary links `libsecret-1.so.0`; no `Depends` entry is hand-written.
- `dpkg -i` installs the Astra package, `xvfb-run -a qtranslate --version` prints
  `QTranslate 0.12.0`, `desktop-file-validate` passes, `dpkg -L` lists the files, and `dpkg -r`
  removes the package without leftovers.
- `dpkg-checkbuilddeps` is clean; `apt-get build-dep` is recorded, or the missing `deb-src` entries
  are recorded as the reason it was not run.
- `DEB_BUILD_OPTIONS=nocheck` skips the tests, `nodoc` skips the package documentation (the
  copyright and changelog are still installed by debhelper), and `parallel=N` is honoured. Without
  `nocheck`, `debian/rules` runs the 45 tests under `xvfb-run`; on Astra the passive-grab X11 test
  binaries need the `/etc/X11/trusted.d/qtranslate` entries and
  `sudo /usr/bin/fly-set-execid.sh`; if that cannot be prepared, the exact commands are recorded
  and the existing 45/45 CTest remains the test gate, not a new pass.
- `debian/source/options` excludes `build`, `build-*`, and `out`; a source package built with a
  `build/` sentinel contains none of them; `dpkg-source -x` extracts the `.dsc`;
  `dpkg-buildpackage -S` and `-b` work; `-tc` cleans the tree; `debian/changelog` and
  `debian/copyright` are consistent.
- `packaging/linux/verify-deb.sh` fails on a missing file, a wrong `Depends`, a bundle file, or a
  hardcoded dependency, and passes on the Astra and trixie packages.
- No application code, tests, `CMakeLists.txt`, or `packaging/linux/package.sh` is changed; the
  only new non-`debian/` file is `packaging/linux/verify-deb.sh`. No license is added; no push; the
  coordinator commits each task; the lead commits the map.

## Depends on

None technically. The Linux packaging stage `02-linux/07-packaging-and-ci` is `done`; the current
`.deb` content and metadata in `packaging/linux/package.sh` `build_deb()` are the reference. The
working tree is clean at `dfd6f2e`.

## Risks and what is needed from the user

- **No license file.** `debian/copyright` is mandatory. It states all rights reserved and does not
  grant a license; this is packaging metadata, not a license choice. The user may choose a project
  license later; this stage does not add one and does not block on it.
- **Astra trusted-client for the X11 tests.** The tests without `nocheck` run the passive-grab X11
  binaries; on Astra the Parsec-patched server denies them until the binaries are in
  `/etc/X11/trusted.d/qtranslate` and `sudo /usr/bin/fly-set-execid.sh` has run. The build path is
  `build/debian/tests/...`; the user (or a coordinator with sudo) must prepare it. The
  `nocheck` build is the user's normal command and does not need this.
- **The `+astra1` version.** A `3.0 (native)` package has one version in `debian/changelog`. The
  current Astra artifact uses `0.12.0+astra1`; whether to keep the suffix and how to produce it
  (a temporary changelog entry, a separate source tree, or dropping it) is a stage 2 decision. This
  stage builds `0.12.0`.
- **Host differences.** `debhelper` 13.11.4+b7 on Astra and 13.16 in trixie; Qt 6.10.2 on Astra
  and 6.8.2 in trixie; `libglib2.0-0` on Astra and `libglib2.0-0t64` in trixie; `libxtst6`
  2:1.2.3 on Astra. Runtime names and version floors must come from `dh_shlibdeps`, never from a
  hand-written list. `dpkg-buildpackage` 1.21.22 on Astra rejects a trailing `.`; the command is
  without it.
- **`libsecret` is optional in code.** `libsecret-1-dev` is in `Build-Depends`, so this package
  links libsecret and its `Depends` includes `libsecret-1-0`. The no-libsecret fallback stays a
  code-level path and a manual CMake build check, not a second package variant.
- **No code change, no push.** `src/`, `tests/`, `CMakeLists.txt`, and
  `packaging/linux/package.sh` stay unchanged; the release integration is stage 2. The coordinator
  commits the task; the lead commits the map; the roadmap work does not push.
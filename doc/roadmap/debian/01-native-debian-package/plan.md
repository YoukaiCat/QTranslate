---
kind: stage
status: done
base: dfd6f2e
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
   and `.claude` exclusions plus a bare `tar-ignore` line that keeps dpkg-source's default `-I`
   list, `debian/changelog`/`copyright`, a clean-copy build, `dpkg-source -b`/`-x`,
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
  `usr/share/doc/qtranslate/copyright` and `changelog.gz` (a `3.0 (native)` package does not
  install `changelog.Debian.gz`); no `usr/bin/qt.conf`, `usr/share/qt6`, `usr/lib`, `usr/plugins`,
  or `usr/translations` are present.
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
  `nocheck`, `debian/rules` runs the 45 tests under `xvfb-run`; the measured run passed 45/45
  without the `/etc/X11/trusted.d/qtranslate` entries under Xvfb (a real Parsec X server may still
  need them; `debian/README.Debian` documents that).
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

## Outcome

Accepted on 2026-11-14 over `dfd6f2e..HEAD` (`8185ecc`, `f69a617`, `5c08c21`, `ee2e971`); the
acceptance run found no defect and added no commit. The repository is a native Debian source
package and the user's command works on Astra Linux 1.8.7.

- `debian/` has `control`, `rules` (0755), `changelog` (`0.12.0`), `copyright` (DEP-5, all rights
  reserved), `source/format` (`3.0 (native)`), `source/options`, `.gitignore` and `README.Debian`.
- From a clean copy, `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage
  -rfakeroot -us -uc` (no `.`) exits 0 and writes `qtranslate_0.12.0_amd64.deb`,
  `qtranslate-dbgsym_0.12.0_amd64.deb`, `.dsc`, `.tar.xz`, `.buildinfo` and `.changes`.
- The package holds `usr/bin/qtranslate`, the desktop entry, the hicolor icon, `copyright` and
  `changelog.gz`; no bundle files and no maintainer scripts. `Depends` is `dh_shlibdeps` output:
  on Astra `libglib2.0-0 (>= 2.12.0)`, `libsecret-1-0 (>= 0.7)`, plain `libxtst6`, no
  `libglib2.0-0t64`; on trixie `libglib2.0-0t64` and `libxtst6 (>= 2:1.2.5)`.
- `dpkg -i`/`dpkg -L`/`desktop-file-validate`/`xvfb-run -a qtranslate --version`
  (`QTranslate 0.12.0`)/`dpkg -r` pass without leftovers. Without `nocheck` the Debian build runs
  45/45 tests under `xvfb-run`; `nodoc` removes `README.Debian` while keeping `copyright` and
  `changelog.gz`; `parallel=N` reaches `ninja -jN`.
- `packaging/linux/verify-deb.sh` runs 15 checks and passes on both the Astra and trixie packages;
  `--self-test` fails the four mutations as intended on Astra and reports one explicit
  `SELFTEST SKIP` for `hardcoded-dep` on trixie, where the host itself requires
  `libxtst6 (>= 2:1.2.5)`.
- The source package extracts with `dpkg-source -x`, `-S` and `-b` build from it, `-tc` and
  `debian/rules clean` leave the tree clean and a second build succeeds. A bare `tar-ignore` line
  restores dpkg-source's default `-I` list, so a tree with `.git` (19 MB) tars 573 entries and no
  `.git/`, while `build/`, `build-local/`, `out/` and `.claude/` sentinels stay out; the default
  list also drops both `.gitignore` files and `.gitattributes`.
- Whole project: Release and Debug build without warnings and pass 45/45 CTest.

What the work found:

- `dh_installdocs` in debhelper 13.11.4 installs `README.Debian` before the `nodoc` check, so
  `debian/rules` has an `override_dh_installdocs` with `--exclude=README.Debian` when `nodoc` is
  set. `override_dh_icons:` (no commands) keeps the package free of maintainer scripts.
- A `3.0 (native)` package installs `changelog.gz`, not `changelog.Debian.gz`; the stage text was
  corrected accordingly.
- A valued `tar-ignore` line replaces dpkg-source's default `-I` list instead of extending it; the
  original four lines shipped the whole `.git` (2627 entries). The bare line fixes it and keeps
  `*.o`, `*.so`, `.svn` and the rest excluded.
- `dh_strip` produces a separate `qtranslate-dbgsym` package; whether to publish it is a stage 2
  decision.
- `lintian` reports `E: build-depends-on-build-essential`; `debian/control` was out of this
  stage's scope and the tag is left for stage 2/3.
- The Debian build without `nocheck` passed 45/45 under Xvfb without the
  `/etc/X11/trusted.d/qtranslate` entries; a real Parsec X server may still need them, and
  `README.Debian` documents that.

Deferred to stage 2: `packaging/linux/package.sh` and the CI job still build the old manual `.deb`
and must switch to the native package; `+astra1`, `dbgsym` publication, artifact names and
checksums are stage 2 decisions. Deferred to stage 3: the README, architecture, handoff and
`AGENTS.md` updates and the final acceptance.
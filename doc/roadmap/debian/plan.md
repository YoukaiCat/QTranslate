---
kind: root
status: in-progress
lang: en
source: doc/architecture.md
branch: main
base: dfd6f2e
---

# Native Debian packaging — roadmap

QTranslate currently ships a `.deb` built by `packaging/linux/package.sh`: `build_deb()` stages
the CMake install tree, writes `DEBIAN/control`, runs `dpkg-shlibdeps` and calls
`dpkg-deb --build`. The repository has no top-level `debian/`, so the standard workflow fails:

```
DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc
```

The user wants a native Debian source package: a top-level `debian/` with `control`, `rules`,
`changelog`, `copyright` and `source/format`, built by `dpkg-buildpackage` on Astra Linux 1.8.7.
There is no trailing `.` in the command; `dpkg-buildpackage` 1.21.22 on Astra rejects it (`unknown
option or argument .`), and `.` is not an argument of `dpkg-buildpackage`.

This map replaces the manual `.deb` with the native package. The portable `tar.gz`, the AppImage,
`SHA256SUMS-linux.txt`, and the CI/release flow stay; the `.deb` becomes the output of
`dpkg-buildpackage`. The change is packaging only: application code, tests and the UI do not
change. The existing `qtranslate` roadmap stays `in-progress` at `06-llm-actions` and is not
changed; this map is a separate, temporary feature map for the Debian packaging work.

The reference is the current `build_deb()` package: exactly `usr/bin/qtranslate`,
`usr/share/applications/qtranslate.desktop`,
`usr/share/icons/hicolor/256x256/apps/qtranslate.png`, with `Depends` from `dpkg-shlibdeps` and no
maintainer scripts. A native package adds the standard `usr/share/doc/qtranslate/copyright` and
`changelog.Debian.gz` (and `README.Debian` unless `nodoc`); that difference is expected and is
recorded in the stage criteria instead of being treated as a regression.

## Done when

- A top-level `debian/` exists with `control`, `rules`, `changelog`, `copyright`, `source/format`,
  `source/options`, `debian/.gitignore`, and `debian/README.Debian`; `debian/rules` is executable;
  `debian/changelog` is `0.12.0`, matching `project(qtranslate VERSION 0.12.0)` in
  `CMakeLists.txt`; `debian/source/format` is `3.0 (native)`.
- On Astra Linux 1.8.7, from a clean copy, the exact command
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  (no trailing `.`) exits 0 and writes `qtranslate_0.12.0_amd64.deb`,
  `qtranslate_0.12.0.dsc`, and `qtranslate_0.12.0.tar.xz` to the parent directory. The `.deb`
  installs with `dpkg -i`, runs `qtranslate --version` under `xvfb-run`, passes
  `desktop-file-validate`, and is removed with `dpkg -r`.
- `dpkg-deb --contents` and `dpkg-deb --info` show the three application files and the standard
  Debian documentation; no bundle files (`usr/bin/qt.conf`, `usr/share/qt6`, `usr/lib`,
  `usr/plugins`, `usr/translations`) are present. `Depends` comes from `dh_shlibdeps`, including
  `libsecret-1-0` only when the binary links `libsecret-1.so.0`; `libglib2.0-0` on Astra and
  `libglib2.0-0t64` on trixie are produced by `dh_shlibdeps`, never hardcoded.
- `dpkg-checkbuilddeps` is clean; `apt-get build-dep` is recorded (or its `deb-src` prerequisite is
  recorded); `DEB_BUILD_OPTIONS=nocheck` skips the tests, `nodoc` skips the package documentation,
  and `parallel=N` is honoured. Without `nocheck`, `debian/rules` runs the 45 tests under
  `xvfb-run`; on Astra the passive-grab X11 test binaries need the `/etc/X11/trusted.d/qtranslate`
  entries and `sudo /usr/bin/fly-set-execid.sh`; if that cannot be prepared, the exact commands are
  recorded and the test gate is the existing 45/45 CTest, not a new pass.
- `debian/source/options` excludes `build`, `build-*`, and `out`; a source package built with a
  `build/` sentinel contains none of them; `dpkg-source -x` extracts the `.dsc`;
  `dpkg-buildpackage -S`/`-b` and `-tc` work; `debian/changelog` and `debian/copyright` are
  consistent and the version matches CMake.
- `packaging/linux/package.sh` no longer builds the `.deb` manually; the CI Linux job and the
  release job build the native `.deb` with `dpkg-buildpackage`; the `tar.gz`, AppImage,
  `SHA256SUMS-linux.txt`, artifact names, and the `qtranslate-linux-x64` upload are preserved; the
  native Astra package is built and the `+astra1` / `debian/changelog` version decision is recorded
  (a user decision if it changes the artifact name or release asset).
- The READMEs, `doc/architecture.md`, `doc/handoff.md`, `AGENTS.md`, and `doc/roadmap/README.md`
  describe the native package, the exact command without `.`, the `nocheck`/`nodoc`/`parallel`
  options, and the Astra trusted-client step; all relative links resolve; the `qtranslate` map's
  nodes are unchanged at `06-llm-actions` (only a journal note was added).
- No application code, tests, `CMakeLists.txt`, or `packaging/linux/package.sh` is changed in stage
  1; stage 2 changes only the packaging and CI files. No license is added: `debian/copyright` says
  all rights reserved and the license question is a recorded risk. No push; the coordinator commits
  each task and the lead commits the map.

## History

Created on 2026-11-13 at `dfd6f2e` (branch `main`). Before this map the `.deb` was built by
`packaging/linux/package.sh` `build_deb()`, which stages the CMake install tree, writes
`DEBIAN/control`, runs `dpkg-shlibdeps` against the staged binary and calls
`dpkg-deb --build --root-owner-group`; the package had three files and no maintainer scripts. The
Linux stage `02-linux/07-packaging-and-ci` of the `qtranslate` map delivered that script, the
AppImage and the Astra target; its verification is in `doc/roadmap/qtranslate/02-linux/`. The new
map is a packaging-only change; the `qtranslate` map remains `in-progress` at `06-llm-actions`.

## Stages

1. [Native Debian package](01-native-debian-package/plan.md) — `in-progress`.
   - **Goal:** add the top-level `debian/` source package and make the exact `dpkg-buildpackage`
     command build, install, remove, and test it on Astra Linux 1.8.7.
   - **What to do, what is touched:** `debian/control`, `debian/rules`, `debian/changelog`,
     `debian/copyright`, `debian/source/format`, `debian/source/options`, `debian/.gitignore`,
     `debian/README.Debian`; a `packaging/linux/verify-deb.sh` gate; the source package hygiene;
     no application code and no `packaging/linux/package.sh` change.
   - **Done when:** the stage's `## Done when` holds: the exact command builds the `.deb` and
     source package, the package contents and `Depends` from `dh_shlibdeps` are verified, the
     package installs and removes, `nocheck`/`nodoc`/`parallel` and the tests behave, and the
     source package excludes the local build directories.
   - **Depends on:** none technically; the Linux packaging stage `02-linux/07-packaging-and-ci`
     is `done`, and the tree is clean at `dfd6f2e`.
   - **Risks and what is needed from the user:** no license file (`debian/copyright` says all
     rights reserved; a license choice is a later decision); the Astra trusted-client entries for
     the passive-grab X11 tests; the `+astra1` version question is deferred to stage 2;
     `libsecret` is optional in code but is a build dependency of this package; `debhelper`
     13.11.4 on Astra and 13.16 in trixie; Qt 6.10.2 on Astra and 6.8.2 in trixie; no push.

2. [Release integration](02-release-integration/plan.md) — `todo`.
   - **Goal:** make the native `.deb` the release artifact: remove the manual `.deb` from
     `packaging/linux/package.sh`, build the native `.deb` in CI and the release job, keep the
     `tar.gz`/AppImage, names and checksums, and produce the native Astra package.
   - **What to do, what is touched:** `packaging/linux/package.sh` (remove `build_deb()`,
     `deb_root`, the manual `DEBIAN/control`, `dpkg-shlibdeps` and `dpkg-deb --build`; keep the
     tar.gz, AppImage, `--target astra`, the bundle and `write_checksums`), `debian/` build
     integration, `.github/workflows/build.yml` (Linux job and release job),
     `packaging/linux/trixie-packages.txt` (`debhelper`, `fakeroot`, `dpkg-dev` is already there),
     the native Astra build and `SHA256SUMS-linux.txt`.
   - **Done when:** `package.sh` has no manual `.deb` staging; CI builds the native `.deb` in
     `debian:trixie`, runs `verify-deb.sh` and the tests, and uploads `qtranslate-linux-x64`; the
     release job publishes the same artifact set; the `tar.gz`, AppImage, checksums and names are
     unchanged; the native Astra package is built and the `+astra1` decision is recorded; no
     tag/push/release without the user.
   - **Depends on:** stage 1.
   - **Risks and what is needed from the user:** the `+astra1` / `debian/changelog` version is a
     user-facing artifact-name decision; CI needs the native build dependencies (the `deb-src`
     entries for `apt-get build-dep` are optional); `fakeroot`; release approval and no push.

3. [Documentation and acceptance](03-docs-and-acceptance/plan.md) — `todo`.
   - **Goal:** document the native package and accept the feature end to end.
   - **What to do, what is touched:** `README.md`/`README.ru.md`, `doc/architecture.md`,
     `doc/handoff.md`, `AGENTS.md`, `doc/roadmap/README.md`; the exact command without `.`, the
     `debian/` layout, `nocheck`/`nodoc`/`parallel`, the Astra trusted-client step, the native
     `.deb` in CI/release and the artifact names; the final acceptance.
   - **Done when:** the documents are consistent and all relative links resolve; no document says
     `package.sh` builds the `.deb` manually; the Linux tests (45/45) and the native `.deb`
     install/remove pass; Windows is recorded as not performed; the root `## Done when` has
     evidence; the `qtranslate` map is unchanged; no push.
   - **Depends on:** stages 1 and 2.
   - **Risks and what is needed from the user:** the all-rights-reserved copyright wording; the
     `+astra1` choice and release approval; Windows is not built here (no MSVC); no push.
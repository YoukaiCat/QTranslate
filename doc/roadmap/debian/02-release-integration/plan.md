---
kind: stage
status: in-progress
base: 2e560e7
depends: [01-native-debian-package]
---

# Release integration

## Goal

Make the native `.deb` the release artifact. `packaging/linux/package.sh` must stop building the
`.deb` by hand: the native package from `debian/` is built by `dpkg-buildpackage`, in the
`debian:trixie` CI job and in the release job, with the same `qtranslate_<version>_amd64.deb` name
and the same `qtranslate-linux-x64` upload. The portable `tar.gz`, the AppImage,
`SHA256SUMS-linux.txt`, and the Astra `--target astra` artifacts stay. The native Astra package is
built on Astra, and the `+astra1` / `debian/changelog` version question is settled and recorded.

## What to do, what is touched

- `packaging/linux/package.sh`: remove `build_deb()` and the manual `deb_root`, `DEBIAN/control`,
  `dpkg-shlibdeps`, and `dpkg-deb --build` staging; keep the CMake install/bundle, the tar.gz, the
  AppImage, the `--target trixie|astra` behavior, the output names, and `SHA256SUMS-linux.txt`.
  Decide whether the native `.deb` is produced by the script calling `dpkg-buildpackage` or
  `debian/rules` or by the CI job next to the script; either way the `.deb` in
  `build/artifacts-linux` is the `dpkg-buildpackage` output, not a manually assembled staging tree.
  Decided below: the script calls `dpkg-buildpackage` itself, so the CI job keeps one `Package`
  step.
- `.github/workflows/build.yml`: the Linux job on `debian:trixie` installs the native build
  dependencies, runs `dpkg-buildpackage` (or `debian/rules binary`), runs
  `packaging/linux/verify-deb.sh` from stage 1, runs `timeout 300 xvfb-run -a ctest`, runs
  `package.sh` for the tar.gz/AppImage, collects the native `.deb`, and uploads
  `qtranslate-linux-x64`; the release job keeps `needs: [windows, linux]`, downloads both
  artifacts, and publishes on a `v*` tag.
- `packaging/linux/trixie-packages.txt`: add `debhelper` and `fakeroot` (and keep `dpkg-dev`);
  the list stays the single package list for `trixie.Dockerfile` and CI. `apt-get build-dep .`
  needs `deb-src` entries; if they are not added, the CI installs the `Build-Depends` from the
  package list and records the choice.
- Native Astra build: `package.sh --target astra` runs `dpkg-buildpackage` on Astra and produces
  the native Astra `.deb` with the version `debian/changelog` carries. The user settled the
  version: **drop `+astra1`** — both the Astra and the trixie `.deb` are `0.12.0`, and
  `SHA256SUMS-linux-astra.txt` keeps the Astra set apart (journal, 2026-11-14). The user also
  settled that `qtranslate-dbgsym` is built but not published. The README,
  `doc/architecture.md` and `doc/handoff.md` name updates are documentation and belong to stage 3
  (`03-docs-and-acceptance`), which lists the artifact names in its scope.
- `SHA256SUMS-linux.txt`: includes the native `.deb`; `SHA256SUMS-linux-astra.txt` includes the
  Astra `.deb`; the tar.gz, AppImage, and Astra names stay; the release artifacts remain
  CI-built.
- The release notes and tag process stay as they are; no version bump, tag, push, or release
  without the user's explicit approval.

## Children

The work is split into three tasks. The first is the risky one (the release script changes
shape); the others build on its artifact set.

1. [01-package-script-native-deb](01-package-script-native-deb/plan.md) — task, detailed.
   `package.sh` drops the manual `.deb` staging and builds the `.deb` with `dpkg-buildpackage` in
   a clean copy under `<output-dir>/.qtranslate-staging`; the Astra target loses `+astra1`; the
   Astra artifact set and the stage-1 gate are verified on Astra Linux 1.8.7.
2. [02-ci-native-deb](02-ci-native-deb/plan.md) — task, `depends: [01-package-script-native-deb]`.
   The trixie CI job installs `debhelper`, `fakeroot` and `pkg-config`, runs the stage-1
   `verify-deb.sh` on the native `.deb`, keeps the 45 tests and the `qtranslate-linux-x64` upload,
   and the release job stays as it is; the job steps are reproduced in a local `debian:trixie`
   container.
3. [03-control-build-essential](03-control-build-essential/plan.md) — task. `debian/control` drops
   `build-essential` from `Build-Depends` (fixes `lintian E: build-depends-on-build-essential`);
   `dpkg-checkbuilddeps`, the native build and the gate still pass on Astra and in `debian:trixie`.

Decisions taken here and recorded in the children: the `dpkg-buildpackage` call lives in
`package.sh`, not in the CI job, so the CI job keeps one `Package` step and the Astra flow stays
one command; the test suite is not run twice (the native `.deb` build uses `nocheck`, the CTest
step remains the test gate); the `.deb` is built from a clean copy of the tree, so the working
tree and the repository's parent directory stay untouched and the unpublished `dbgsym`,
`.changes` and `.buildinfo` never reach `build/artifacts-linux`. After dropping `+astra1` the
trixie and Astra `.deb` files have the same name, so the two sets are built into different output
directories; the script's usage text says so.

## Done when

- `packaging/linux/package.sh` has no `build_deb()`, `deb_root`, manual `DEBIAN/control`,
  `dpkg-shlibdeps`, or `dpkg-deb --build` path; the `.deb` in `build/artifacts-linux` is the
  `dpkg-buildpackage` output.
- The Linux CI job builds the native `.deb` in `debian:trixie`, runs the stage-1
  `verify-deb.sh`, runs the tests under `xvfb-run`, runs `package.sh` for the tar.gz/AppImage, and
  uploads `qtranslate-linux-x64`; the release job publishes the same artifact set; the YAML
  parses and the job invariants are recorded.
- The native Astra package is built on Astra, `verify-deb.sh` passes, and the `+astra1` decision
  is applied: `qtranslate_0.12.0_amd64.deb`, no suffix, `debian/changelog` unchanged; the CI and
  checksums follow the name. The README, `doc/architecture.md` and `doc/handoff.md` name updates
  are stage 3 (`03-docs-and-acceptance`), which lists them in its scope.
- `SHA256SUMS-linux.txt` (and, for Astra, `SHA256SUMS-linux-astra.txt`) includes the native
  `.deb`; the tar.gz/AppImage names and the release asset set are preserved.
- No application code or tests change; `debian/` and the packaging/CI files are the scope; a real
  GitHub run, tag, and release are not performed without the user's approval.

## Depends on

Stage 1 (`01-native-debian-package`), especially `01-debian-skeleton-and-build` and
`02-package-content-and-dependencies`.

## Risks and what is needed from the user

- **`+astra1` (settled by the user).** A `3.0 (native)` package has exactly one version, and the
  user dropped the suffix: `debian/changelog` stays `0.12.0` and both `.deb` files are
  `qtranslate_0.12.0_amd64.deb`. The Astra set is separated by `SHA256SUMS-linux-astra.txt` and by
  the `-astra` tarball name, not by the `.deb` name; the two sets must therefore be built into
  different output directories. `qtranslate-dbgsym` is built and not published.
- **CI build dependencies.** The Linux image has `deb` sources only; `apt-get build-dep` needs
  `deb-src` entries. The fallback is the explicit `trixie-packages.txt` list plus
  `dpkg-checkbuilddeps`; the stage records which path CI uses.
- **`fakeroot` and `dpkg-buildpackage`.** The CI container runs as root; `-rfakeroot` is still the
  documented user command. The native build must work in the container and on Astra; the release
  job is unchanged.
- **The portable artifacts.** The tar.gz and AppImage still bundle trixie Qt and are not affected
  by the native `.deb`; the Astra artifacts stay native Astra builds. Do not accidentally remove
  the `--target astra` branch or the AppImage tool pins.
- **Release approval.** Pushing, tagging, and publishing are the user's decisions; this stage
  prepares the change and runs local/container gates only.
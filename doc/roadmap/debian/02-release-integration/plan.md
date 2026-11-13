---
kind: stage
status: todo
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
- Native Astra build: `package.sh --target astra` or a documented `dpkg-buildpackage` run on Astra
  produces the native Astra `.deb`. The `+astra1` version must be in `debian/changelog` for that
  build; decide whether to use a temporary changelog entry, a separate source tree, or to drop the
  suffix. The artifact naming and the README/architecture references follow the decision.
- `SHA256SUMS-linux.txt`: includes the native `.deb`; `SHA256SUMS-linux-astra.txt` includes the
  Astra `.deb`; the tar.gz, AppImage, and Astra names stay; the release artifacts remain
  CI-built.
- The release notes and tag process stay as they are; no version bump, tag, push, or release
  without the user's explicit approval.

## Done when

- `packaging/linux/package.sh` has no `build_deb()`, `deb_root`, manual `DEBIAN/control`,
  `dpkg-shlibdeps`, or `dpkg-deb --build` path; the `.deb` in `build/artifacts-linux` is the
  `dpkg-buildpackage` output.
- The Linux CI job builds the native `.deb` in `debian:trixie`, runs the stage-1
  `verify-deb.sh`, runs the tests under `xvfb-run`, runs `package.sh` for the tar.gz/AppImage, and
  uploads `qtranslate-linux-x64`; the release job publishes the same artifact set; the YAML
  parses and the job invariants are recorded.
- The native Astra package is built on Astra, `verify-deb.sh` passes, and the `+astra1` /
  `debian/changelog` decision is recorded with its artifact name; if the user chooses a different
  name, the READMEs, `doc/architecture.md`, CI, and checksums are updated.
- `SHA256SUMS-linux.txt` (and, for Astra, `SHA256SUMS-linux-astra.txt`) includes the native
  `.deb`; the tar.gz/AppImage names and the release asset set are preserved.
- No application code or tests change; `debian/` and the packaging/CI files are the scope; a real
  GitHub run, tag, and release are not performed without the user's approval.

## Depends on

Stage 1 (`01-native-debian-package`), especially `01-debian-skeleton-and-build` and
`02-package-content-and-dependencies`.

## Risks and what is needed from the user

- **`+astra1` and the `debian/changelog` version.** A `3.0 (native)` package has exactly one
  version. The current Astra artifact is `0.12.0+astra1`; keeping the suffix changes the
  `debian/changelog` for the Astra build and may change the artifact name. This is a user-facing
  artifact/release decision and must be settled with the user before the Astra package is
  published; the stage can build the trixie package first.
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
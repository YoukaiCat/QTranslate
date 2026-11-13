---
kind: task
status: todo
---

# Drop `build-essential` from `Build-Depends`

## Task

`lintian` reports `E: build-depends-on-build-essential` for the source package built in stage 1
(`debian/control:8`; recorded in the stage-1 outcome and in the journal as an item for this
stage). `build-essential` is guaranteed by the build environment and must not be listed in
`Build-Depends`; Debian policy and lintian both say so, and the tag is an error, not a style
warning.

The build environment keeps the metapackage (`packaging/linux/trixie-packages.txt:10` lists
`build-essential`, which is correct for an image that must have `gcc`/`g++`/`make`); only the
source-package metadata changes. The rest of the `Build-Depends` field is unchanged:
`debhelper-compat (= 13)`, `cmake (>= 3.19)`, `ninja-build`, `qt6-base-dev (>= 6.5)`,
`qt6-tools-dev`, `qt6-tools-dev-tools`, `libx11-dev`, `libxtst-dev`, `libsecret-1-dev`,
`pkg-config`, `desktop-file-utils`, `dbus`, `xvfb`, `xauth`. Nothing in `CMakeLists.txt` or
`debian/rules` names `build-essential`; CMake finds the compiler through the environment.

If a clean environment turns out to need an explicit compiler dependency (it should not, because
`debhelper`/`cmake` only need a working toolchain that the environment provides), record the
finding and the decision instead of silently re-adding the entry.

## Done when

- `debian/control` no longer lists `build-essential`; `dpkg-checkbuilddeps` in the repository root
  is silent and exits 0 on Astra Linux 1.8.7 and in a `debian:trixie` container with
  `packaging/linux/trixie-packages.txt` installed.
- `lintian` on the rebuilt source package (`dpkg-buildpackage -S`, then
  `lintian qtranslate_0.12.0.dsc`) has no `build-depends-on-build-essential`; every other lintian
  tag is recorded as found and not fixed here.
- The native build still succeeds on both hosts: `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck"
  dpkg-buildpackage -rfakeroot -us -uc` from a clean copy exits 0 on Astra, and the trixie
  container build from `02-ci-native-deb` exits 0; `packaging/linux/verify-deb.sh` still reports
  `15 checks, 15 passed, 0 failed` on both packages.
- Besides this plan, only `debian/control` is changed; `debian/changelog` and `debian/rules` are
  untouched, and the version stays `0.12.0`.

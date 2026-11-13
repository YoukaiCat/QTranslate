---
kind: task
status: todo
depends: [01-package-script-native-deb]
---

# Native `.deb` in the trixie CI job

## Task

The Linux CI job (`.github/workflows/build.yml:86-125`) runs on `ubuntu-latest` in the
`debian:trixie` container: it installs `packaging/linux/trixie-packages.txt`, builds the CMake
tree in `build/ci`, runs `timeout 300 xvfb-run -a ctest --test-dir build/ci`, then
`./packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux` and
uploads `build/artifacts-linux/*` as `qtranslate-linux-x64`. Since `01-package-script-native-deb`
the package step builds the `.deb` with `dpkg-buildpackage` from inside the script, so the job
needs the native build/packaging dependencies and the stage-1 `packaging/linux/verify-deb.sh`
gate, while the artifact set, the names and the upload stay as they are.

What has to change and why:

- `packaging/linux/trixie-packages.txt`: add `debhelper` (the `dh` sequence that
  `debian/rules:11` runs; `debian/control:8` asks for `debhelper-compat (= 13)`), `fakeroot` (the
  documented `dpkg-buildpackage -rfakeroot -us -uc` command) and `pkg-config` (`debhelper`,
  `fakeroot` and `pkg-config` were installed by hand in the stage-1 trixie container;
  `pkg-config` is in `debian/control:10` and is not in the list today, so
  `dpkg-checkbuilddeps` would fail). `dpkg-dev` is already listed (`:38`). Keep the section
  layout and comments; `trixie.Dockerfile` reads the same file and needs no change.
- `.github/workflows/build.yml` Linux job: after the package step, run
  `./packaging/linux/verify-deb.sh build/artifacts-linux/qtranslate_0.12.0_amd64.deb` (or a
  `build/artifacts-linux/qtranslate_*_amd64.deb` glob that cannot match `qtranslate-dbgsym_*`).
  The gate installs and removes the package in the container, compares `Depends` with the host's
  `dpkg-shlibdeps`, checks the contents and the desktop entry, and runs
  `xvfb-run -a qtranslate --version`; `xvfb`, `xauth`, `desktop-file-utils` and `dbus` are
  already in the list.
- Do not build or run tests twice: the test gate stays the existing
  `timeout 300 xvfb-run -a ctest` step, and `package.sh` builds the `.deb` with
  `DEB_BUILD_OPTIONS=... nocheck` (child 01), so the `.deb` build neither builds nor runs the
  tests. The `.deb` build is a second Release compile without tests; that cost is accepted
  rather than reused from `build/ci`, because `debian/rules` builds in `build/debian` with the
  system Qt and must not share a tree with the packaging install tree.
- `apt-get build-dep` is not used: `debian:trixie` has no `deb-src` entries. The shared package
  list plus the implicit `dpkg-checkbuilddeps` inside `dpkg-buildpackage` is the recorded choice.
- The release job (`:127-178`) needs no change: it runs on a `v*` tag, needs both jobs, downloads
  `qtranslate-windows-x64` and `qtranslate-linux-x64` and publishes `artifacts/*`; the native
  `.deb`, `SHA256SUMS-linux.txt` and the AppImage arrive through the unchanged upload. Verify and
  record the invariants instead of editing it.

## Done when

- A local `debian:trixie` run of the job's steps on a fresh copy of the tree exits 0, in this
  order: `apt-get install` the package list (including the three new entries), CMake configure and
  build in `build/ci`, `timeout 300 xvfb-run -a ctest --test-dir build/ci` with 45/45,
  `./packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux`, and
  `./packaging/linux/verify-deb.sh build/artifacts-linux/qtranslate_0.12.0_amd64.deb` ending with
  `15 checks, 15 passed, 0 failed`.
- `build/artifacts-linux` holds `qtranslate-0.12.0-linux-x86_64.tar.gz`,
  `qtranslate_0.12.0_amd64.deb`, `qtranslate-0.12.0-x86_64.AppImage` and
  `SHA256SUMS-linux.txt`; `sha256sum -c SHA256SUMS-linux.txt` passes; there is no `dbgsym`,
  `.changes` or `.buildinfo`.
- `dpkg-checkbuilddeps` in the container is silent with the package list installed (this is also
  the check that `pkg-config` was missing).
- `.github/workflows/build.yml` parses as YAML (`ruby -ryaml -e 'YAML.load_file(".github/workflows/build.yml")'`
  on this host, which has no `python3-yaml`), and the job invariants hold: the Linux job keeps
  `container: debian:trixie`, the `timeout 300 xvfb-run -a ctest` step, the same
  `upload-artifact` name `qtranslate-linux-x64` and path `build/artifacts-linux/*`; the release
  job keeps `if: startsWith(github.ref, 'refs/tags/v')`, `needs: [windows, linux]` and both
  downloads; no step pushes, tags or releases anything.
- No GitHub workflow run, push, tag or release is performed; only the local container run and the
  YAML parse.
- Besides this plan, only `packaging/linux/trixie-packages.txt` and
  `.github/workflows/build.yml` are changed.

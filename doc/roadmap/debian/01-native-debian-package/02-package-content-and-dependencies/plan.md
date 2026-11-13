---
kind: task
status: todo
depends: [01-debian-skeleton-and-build]
---

# Package content and dependency gates

## Task

Add a repeatable gate for the native `.deb` content and dependencies, so the package is accepted
on more than "the build exited 0". The gate is a new executable
`packaging/linux/verify-deb.sh` that takes the path to a `.deb` and checks the package against the
host that produced it (Astra or trixie). It is a verification tool; it does not change
`packaging/linux/package.sh`, the application, or the `debian/` build rules.

The script must check at least:

- `dpkg-deb --contents`: `usr/bin/qtranslate`, `usr/share/applications/qtranslate.desktop`,
  `usr/share/icons/hicolor/256x256/apps/qtranslate.png`, and the standard
  `usr/share/doc/qtranslate/copyright` and `changelog.Debian.gz`; no `usr/bin/qt.conf`,
  `usr/share/qt6`, `usr/lib`, `usr/plugins`, or `usr/translations`.
- `dpkg-deb --info`/`dpkg -I`: `Package`, `Version`, `Architecture`, `Maintainer`, and `Depends`;
  the version equals `dpkg-parsechangelog -S Version` and the CMake project version; no
  maintainer scripts.
- `Depends` from `dh_shlibdeps`: extract the binary and run `dpkg-shlibdeps` with a minimal
  throwaway `debian/control`; compare the generated `shlibs:Depends` with the package's `Depends`.
  The script must not require a hand-written expected list. On Astra it must see
  `libglib2.0-0` and no `libglib2.0-0t64`; on trixie it must see `libglib2.0-0t64`; the
  `libxtst6` floor must be whatever the host's shlibs say, not a constant.
- `libsecret-1-0` only when `readelf -d usr/bin/qtranslate` shows `libsecret-1.so.0`; the
  no-libsecret fallback is not a second package variant.
- `dpkg -i`/`dpkg -r` (or `dpkg -i`/`dpkg -P` in a disposable container), `dpkg -L`,
  `desktop-file-validate`, and `xvfb-run -a qtranslate --version`; remove the package afterwards
  and fail on leftovers.
- `dpkg-checkbuilddeps` in the source tree; `apt-get build-dep .` is attempted when the host has
  `deb-src` entries, otherwise the missing `deb-src` prerequisite is reported, not silently
  skipped.
- A negative path for each check so the gate is meaningful: a missing file, an edited `Depends`,
  a bundle file, and a hardcoded dependency must each fail the script.

Run the gate on the Astra `.deb` from task 01 and on a trixie `.deb` built in the
`qtranslate-trixie` container (or in a disposable `debian:trixie` container). The native build
dependencies (`debhelper`, `fakeroot`, `dpkg-dev`) are installed in the container for the check;
the CI integration is stage 2.

## Done when

- `packaging/linux/verify-deb.sh` exists, is executable, takes the `.deb` path, documents its usage,
  and exits non-zero with a clear message on each failed check.
- The script passes on the Astra `qtranslate_0.12.0_amd64.deb` and on a trixie
  `qtranslate_0.12.0_amd64.deb`; the Astra package's `Depends` is the Astra `dh_shlibdeps` output
  (`libglib2.0-0`, no `t64`, no `libxtst6 (>= 2:1.2.5)`); the trixie package's `Depends` is the
  trixie output (`libglib2.0-0t64`); `libsecret-1-0` is present only when the binary links
  libsecret.
- `dpkg -i`/`dpkg -r`, `dpkg -L`, `desktop-file-validate`, and
  `xvfb-run -a qtranslate --version` pass for the tested package; `dpkg-checkbuilddeps` is clean;
  `apt-get build-dep .` is recorded or its `deb-src` prerequisite is recorded.
- The four negative paths (missing file, edited `Depends`, bundle file, hardcoded dependency) fail
  the script as expected.
- No application code, tests, `CMakeLists.txt`, `packaging/linux/package.sh`, or `debian/` build
  rule changes; no commit or push.
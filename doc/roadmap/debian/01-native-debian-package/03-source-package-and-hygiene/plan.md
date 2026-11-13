---
kind: task
status: todo
depends: [01-debian-skeleton-and-build]
---

# Source package and build hygiene

## Task

Harden the native source package and the clean-build path. The source format is `3.0 (native)`;
the source tarball must contain the repository sources and `debian/`, and must not contain the local
CMake build directories, the CMake outputs, the local AI/IDE state, or `.git`. The task also
verifies the version and copyright metadata and that `debian/rules clean` and `-tc` leave the tree
clean.

The work covers:

- `debian/source/format` is exactly `3.0 (native)`; `debian/source/options` has `tar-ignore` for
  `build`, `build-*`, and `out`, and for the local `.claude` directory. The options are shown by
  `dpkg-source --build`/`--print-format` and are effective: a `build/` sentinel, a `build-local/`
  sentinel, and an `out/` sentinel must not appear in the `.tar.xz`.
- `debian/changelog` has the native version `0.12.0` with no Debian revision;
  `dpkg-parsechangelog -S Version` equals `project(qtranslate VERSION ...)` in `CMakeLists.txt`;
  `debian/copyright` is DEP-5 and states all rights reserved.
- `debian/.gitignore` covers the generated debhelper files (`debian/files`,
  `debian/*.substvars`, `debian/*.debhelper*`, `debian/debhelper-build-stamp`,
  `debian/.debhelper/`, `debian/qtranslate/`); after a build `git status` shows only the tracked
  `debian/` sources.
- A clean-copy build: copy the tree without `.git`, `build`, `build-*`, `out`, and `.claude`;
  run `dpkg-source -b` and inspect the tarball; run `dpkg-source -x` on the `.dsc`; run
  `dpkg-buildpackage -S` and `dpkg-buildpackage -b` from the extracted source; run
  `dpkg-buildpackage -tc` and confirm the post-clean.
- `debian/rules clean` removes `build/debian`, `debian/files`, `debian/qtranslate/`, and the
  generated `debian/*.substvars`; a second build from the same tree starts clean.
- The source package does not include `packaging/linux` build outputs, `build/linux-tools`, or
  the AppImage cache; only the tracked packaging sources.
- The `debian/README.Debian` build instructions match the actual command and options.

## Done when

- `dpkg-source --print-format .` prints `3.0 (native)`; a `dpkg-source -b` run in a copy with
  `build/`, `build-local/`, and `out/` sentinels produces a `.tar.xz` with none of them and no
  `.git`.
- `dpkg-source -x qtranslate_0.12.0.dsc` extracts the source; `dpkg-buildpackage -S` and
  `dpkg-buildpackage -b` run from the extracted source; `dpkg-buildpackage -tc` removes the build
  products and `git status` in the original tree is clean except for the intended tracked files.
- `dpkg-parsechangelog -S Version` is `0.12.0` and equals the CMake project version;
  `debian/copyright` is DEP-5 and states all rights reserved; `debian/changelog` has no Debian
  revision.
- `debian/rules clean` removes `build/debian` and the generated `debian/` files; a second build
  from the same copy succeeds.
- The `debian/README.Debian` command and options match the actual build.
- No application code, tests, `CMakeLists.txt`, `packaging/linux/package.sh`, or native package
  contents change; no commit or push.
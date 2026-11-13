# Debian packaging roadmap journal

## Where we left off

Current node: stage `02-release-integration` is `in-progress` (`base: 2e560e7`) and split into
three tasks. Task [01-package-script-native-deb](02-release-integration/01-package-script-native-deb/plan.md)
is `in-progress`, `detailed: true`; the coordinator is running. The planner's decisions: the script
calls `dpkg-buildpackage` itself (one `Package` step in CI), the `.deb` is built in a clean copy
under `<output-dir>/.qtranslate-staging`, the build uses
`DEB_BUILD_OPTIONS="parallel=N nocheck"` so the 45 tests are not run twice, and `--version` must
equal `debian/changelog`. Open items after it: `02-ci-native-deb` (trixie CI, `verify-deb.sh`,
`trixie-packages.txt` gains `debhelper`/`fakeroot`/`pkg-config`) and `03-control-build-essential`
(drop `build-essential`, clear `lintian E:`). User decisions: **drop `+astra1`**, do not publish
`qtranslate-dbgsym`. The docs that still name `+astra1` are stage 3. Branch `main`. The existing
`qtranslate` map keeps its nodes and stays `in-progress` at `06-llm-actions`; only a journal note
was added there.

## Journal

- 2026-11-13 — Native Debian packaging map created. The user (the project author) wants the
  standard `.deb` workflow: copy the project, enter its directory and run
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`;
  the repository has no top-level `debian/`, so the command fails. The current `.deb` is staged
  manually by `packaging/linux/package.sh` `build_deb()` (CMake install tree, `DEBIAN/control`,
  `dpkg-shlibdeps`, `dpkg-deb --build`), which is not a Debian source package. Decision: replace
  the manual `.deb` with a native `debian/` source package; keep the portable `tar.gz`, the
  AppImage, `SHA256SUMS-linux.txt`, the CI and release flow, and the Astra `--target astra`
  artifacts. This is packaging only; application code and tests do not change.

  Verified on this machine (Astra Linux 1.8.7, `dpkg-buildpackage` 1.21.22, `debhelper`
  13.11.4+b7, `dpkg-dev` 1.21.22, `fakeroot` 1.31, Qt 6.10.2, GCC 12.2): a trailing `.` is
  rejected by `dpkg-buildpackage` (`unknown option or argument .`); `.` is not an argument of
  `dpkg-buildpackage`, so the plan and the documentation use the command without it. Its default
  is the full build (source and binary); the `.deb`, `.dsc`, and `.tar.xz` land in the parent
  directory.

  Facts and constraints: `debhelper` 13.11.4+b7 on Astra and 13.16 in trixie; Qt 6.10.2 on Astra
  and 6.8.2 in trixie; `libsecret-1-dev` is optional in the code; `libglib2.0-0` on Astra and
  `libglib2.0-0t64` in trixie must come from `dh_shlibdeps`, never from a hardcoded list;
  `debian/source/format` is `3.0 (native)`; `debian/source/options` uses `tar-ignore` for `build`,
  `build-*`, `out`, and the local `.claude` directory. The project has no license file, so
  `debian/copyright` is mandatory and states all rights reserved; the license question is a
  recorded risk and does not block the work.

  Structure: three stages — `01-native-debian-package`, `02-release-integration`,
  `03-docs-and-acceptance`. Stage 1 has three tasks: `01-debian-skeleton-and-build` (detailed, the
  first working slice), `02-package-content-and-dependencies` (the `packaging/linux/verify-deb.sh`
  gate), and `03-source-package-and-hygiene` (source format/options, clean-copy build,
  `dpkg-source`, `-tc`). The `+astra1` / `debian/changelog` version for the native Astra package is
  a stage-2 decision and is recorded as a risk, not chosen here. No application code,
  `packaging/linux/package.sh`, or `qtranslate` map node was changed; no commit or push was made by
  the planner.

- 2026-11-13 — Lead corrections before the first coordinator. The planner's `Build-Depends` for the
  first task omitted `desktop-file-utils` (the `desktop-file-validate` check) and `pkg-config` (the
  CMake `PkgConfig` lookup and the optional libsecret); both were added. The `dh` sequence's
  `dh_icons` would add `postinst`/`postrm` for the hicolor icon and contradict the current
  no-maintainer-scripts package, so `debian/rules` gets `override_dh_icons:` with no commands. The
  planner's "no commit" wording is superseded: the coordinator commits the task's `debian/` files
  and `plan.md`, and the lead commits the map root, stage, and journal; no push. The first
  coordinator was started for `01-debian-skeleton-and-build`.

- 2026-11-13 — Task `02-package-content-and-dependencies` done (`5c08c21`):
  `packaging/linux/verify-deb.sh` is a 15-check gate whose expectations come from the host
  (`dpkg-shlibdeps` with the real `Build-Depends`, `dpkg --print-architecture`,
  `dpkg-parsechangelog`, `readelf -d`) plus a `--self-test` mode. Astra: 15/15, self-test 4/4.
  `debian:trixie` (image 13.7, debhelper 13.24.2, Qt 6.8.2, dpkg 1.22.22): build 23 s, gate 15/15,
  `libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`, root install roundtrip; self-test 3 passed + 1 SKIP.
  Decisions worth keeping: the `Depends` comparison is exact and a cross-host package fails with a
  hint instead of being normalised; `apt-get -s build-dep .` is a non-fatal, labelled simulation;
  the install roundtrip skips a package that is already installed or held, and refuses a `.deb`
  whose `Package` is not `qtranslate` (the sibling `dbgsym`). Known limitation recorded in the
  outcome: `hardcoded-dep` cannot be constructed in trixie because trixie itself requires
  `libxtst6 (>= 2:1.2.5)`, so that case is an explicit `SELFTEST SKIP` there, not a silent pass.
  Lead's notes: the plan's step 10 wording about "neither Astra nor trixie" is inaccurate and the
  outcome corrects it; no node is affected, since stage 2 consumes the script as-is.

- 2026-11-14 — Task `03-source-package-and-hygiene` done (`ee2e971`) and stage
  `01-native-debian-package` accepted. Task 03 added the bare `tar-ignore` line, updated
  `debian/README.Debian` (dbgsym/`.buildinfo`/`.changes`, source-package section) and left
  `debian/.gitignore` and `debian/rules` unchanged after measuring the residue. The lead's
  correction to the planner was confirmed: valued `tar-ignore` lines replace dpkg-source's default
  `-I` list (the tarball had 2627 `.git/` entries); the bare line restores it, and the default list
  also excludes both `.gitignore` files and `.gitattributes`. The acceptance run built the package
  from a clean `git archive`, ran the 15-check gate and its self-test, the install roundtrip, the
  source package (`-S`, `-x`, `-b`, `-tc`, clean, second build), the `nocheck`/`nodoc` behaviour
  (45/45 tests without `nocheck`), and Release/Debug 45/45; it found no defect and added no commit.
  Stage outcome records the deferred items: `package.sh`/CI integration, `+astra1`, `dbgsym`
  publication and `lintian E: build-depends-on-build-essential`. Next: stage
  `02-release-integration`, planner in "break down" mode.

- 2026-11-14 — Vendor decisions for stage `02-release-integration` (user). **Drop `+astra1`:** the
  Astra and trixie `.deb` are both `0.12.0`; the user's reasoning is that `+astra1` denotes a
  patched or backported build on top of an "original" package, and QTranslate is the original in
  both cases. The artifact name changes to `qtranslate_0.12.0_amd64.deb` for the Astra target too;
  `SHA256SUMS-linux-astra.txt` still distinguishes the Astra set. **Do not publish `dbgsym`:** the
  debug-symbol package is a normal `dpkg-buildpackage` output but stays out of
  `build/artifacts-linux` and the checksums. Facts confirmed while answering the user: the GitHub
  Linux job runs in a `debian:trixie` container on `ubuntu-latest` (no Astra/ALSE image exists
  there), so CI builds only the trixie `.deb`; the Astra `.deb` is built locally by
  `package.sh --target astra`, which refuses to run off Astra. The two packages differ only by the
  host they were built on: trixie is glibc 2.41, Qt 6.8.2, `libglib2.0-0t64`,
  `libxtst6 (>= 2:1.2.5)`; Astra 1.8.7 is glibc 2.36, Qt 6.10.2, `libglib2.0-0`, plain `libxtst6`.

- 2026-11-14 — Stage `02-release-integration` split into three tasks. Decisions: `package.sh` keeps
  being the single entry point and calls `dpkg-buildpackage` itself (the CI job keeps one `Package`
  step); the `.deb` is built in a clean copy at `<output-dir>/.qtranslate-staging/deb-src`, so the
  working tree and the repository parent stay clean and the unpublished `dbgsym`/`.changes`/
  `.buildinfo` never reach the artifacts; the package build uses
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck"` because the test gate remains the `xvfb-run -a
  ctest` step; `--version` must agree with `debian/changelog`, otherwise the `.deb` name and the
  tarball name could diverge. `dpkg-buildpackage` has no output-dir option on 1.21.22, which is why
  the copy exists. The stage text now points documentation updates to stage 3; the acceptance
  criterion was reworded accordingly (no scope change). `pkg-config` is missing from
  `trixie-packages.txt` although it is in `Build-Depends`; task 02 adds it with `debhelper` and
  `fakeroot`.
# Debian packaging roadmap journal

## Where we left off

Current node: stage `01-native-debian-package` is `done` (accepted on 2026-11-14 over
`dfd6f2e..HEAD`; see its `## Outcome`). The next node is stage `02-release-integration`: it is
`todo` and has no children, so the next step is a planner in "break down" mode. Scope confirmed by
the user: `packaging/linux/package.sh` stops staging the manual `.deb`, the CI Linux job builds the
native package with `dpkg-buildpackage` and runs `packaging/linux/verify-deb.sh`, and the portable
`tar.gz`, AppImage, `SHA256SUMS-linux.txt` and artifact names stay. Open decisions for that stage:
the `+astra1` native version, whether to publish `qtranslate-dbgsym`, and the
`lintian E: build-depends-on-build-essential` tag. Branch `main`, HEAD `ee2e971`. The existing
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
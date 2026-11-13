# Debian packaging roadmap journal

## Where we left off

Current node: stage `01-native-debian-package` is `in-progress`; its first task
[`01-debian-skeleton-and-build`](01-native-debian-package/01-debian-skeleton-and-build/plan.md) is
`in-progress`; the coordinator is running. The lead's corrections before the run: the
`Build-Depends` list adds `desktop-file-utils` and `pkg-config`; `debian/rules` adds
`override_dh_icons:` to keep the no-maintainer-script parity; the coordinator commits the `debian/`
files and the task `plan.md`; the lead commits the map root, stage, and journal. No push. Branch
`main`, base `dfd6f2e`. The existing `qtranslate` map keeps its nodes and stays `in-progress` at
`06-llm-actions`; only a journal note was added there.

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
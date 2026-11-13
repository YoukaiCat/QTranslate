---
kind: stage
status: done
base: b9a6f75
depends: [01-native-debian-package, 02-release-integration]
---

# Documentation and acceptance

## Goal

Document the native Debian package and accept the feature end to end. The documentation must show
the exact `dpkg-buildpackage` command without a trailing `.`, the `debian/` layout, the
`nocheck`/`nodoc`/`parallel` behavior, the test and Astra trusted-client step, and the native
`.deb` in CI and the release. The acceptance checks the root `## Done when` with evidence, not with
build logs alone.

## What to do, what is touched

- `README.md` and `README.ru.md`: replace the manual `packaging/linux/package.sh` `.deb`
  instructions for end users with the native `debian/` build instructions, the exact command
  without `.`, the artifacts in the parent directory, `nocheck`/`nodoc`/`parallel`, and the
  native `.deb` release artifact; keep the portable `tar.gz`/AppImage/Astra instructions and the
  two READMEs in sync.
- `doc/architecture.md`: update the packaging and release sections (the file/layout lists, §3.1,
  §3.4, §9 and the packaging notes): the native source package, `debian/`, `dpkg-buildpackage`,
  the native `.deb` in CI, the artifact names, and the Astra `+astra1` decision.
- `doc/handoff.md`: the current state, the native packaging, the next release, the known
  limitations (all-rights-reserved copyright, `+astra1`, the Astra trusted-client step, the tests
  not performed on Windows).
- `AGENTS.md`: the Linux build/package command (`dpkg-buildpackage`), the `debian/` tree, the CI
  and release flow, and the test counts. Do not add a Jira key.
- `doc/roadmap/README.md`: the `debian` map status and the one-line description; the `qtranslate`
  line stays unchanged.
- Acceptance: the root `## Done when` traceability, the full `git diff`, the Linux 45/45 tests,
  the native `.deb` install/remove, the CI YAML invariants, the documentation links, and the
  `qtranslate` map untouched. Record Windows as not performed (no MSVC). No push.

## Children

1. [01-docs-native-package](01-docs-native-package/plan.md) — task, detailed. Review and finish the
   documentation edits the lead already made in the working tree (`README.md`, `README.ru.md`,
   `AGENTS.md`, `doc/architecture.md`, `doc/handoff.md`, `debian/README.Debian`) and the link,
   EN/RU, `+astra1` and manual-`.deb` gates. Documentation only; the lead commits it as its own
   commit.
2. [02-final-acceptance](02-final-acceptance/plan.md) — task, `depends: [01-docs-native-package]`.
   The coordinator's final acceptance of the root and stage `## Done when` on the final tree: clean
   build, package contents and `Depends`, install/remove, the gate, 45/45 tests, the source
   package, the CI/release invariants, the documentation gates and the scope checks. Windows is
   recorded as not performed; no push, tag or release.

## Done when

- The READMEs, `doc/architecture.md`, `doc/handoff.md`, `AGENTS.md`, and `doc/roadmap/README.md`
  describe the native package; the command is exactly
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  (no trailing `.`); the `debian/` layout, `nocheck`/`nodoc`/`parallel`, and the Astra
  trusted-client step are documented; all relative links resolve.
- No document still says `packaging/linux/package.sh` builds the `.deb` manually; the `tar.gz`,
  AppImage, `SHA256SUMS-linux.txt`, Astra names, and `qtranslate-linux-x64` upload are correct.
- The acceptance runs the Linux tests (45/45) and the native `.deb` install/remove, checks the
  CI/release YAML, and records the Windows result as not performed; the root `## Done when` has
  evidence; no application code changes; no commit or push.
- The `qtranslate` map stays `in-progress` at `06-llm-actions` and its nodes are unchanged.

## Depends on

Stages 1 and 2.

## Risks and what is needed from the user

- **Copyright.** The project has no license file; the documentation and `debian/copyright` say
  all rights reserved. The user may choose a project license later; the acceptance does not
  require a license and does not add one.
- **`+astra1` and release approval.** The native Astra package name/version is the stage-2
  decision; pushing, tagging, and publishing are the user's decisions.
- **Windows.** The Windows build and the 39 tests are not performed here (no MSVC); record them as
  not performed, never as passed.
- **No push.** The roadmap work leaves commits and pushes to the lead and the user.

## Outcome

Accepted on 2026-11-14. Two tasks: `01-docs-native-package` closed by the lead as `c7e5808`
(documentation only), `02-final-acceptance` closed by the coordinator as `8bbef5b` (the outcome
itself, no source change).

- `README.md`, `README.ru.md`, `AGENTS.md`, `doc/architecture.md`, `doc/handoff.md` and
  `debian/README.Debian` document the native source package, the exact
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  command without a trailing dot, the `nocheck`/`nodoc`/`parallel` options, the `verify-deb.sh`
  gate, the native `.deb` in CI and the Astra trusted-client fallback. `+astra1` is no longer a
  current artifact name; `doc/handoff.md` keeps only the two sentences that record the removal.
- The parent-directory outputs are documented as the `.deb`, `.dsc`, `.tar.xz`, `dbgsym`,
  `.buildinfo` and `.changes`, with `dbgsym` not published. `doc/architecture.md` lists `debian/`
  in the root layout and the `doc/roadmap/` subtree and `0.12.0.md` under `releases/`.
  `doc/roadmap/README.md` describes the native package (its status flips to `done` when the root
  closes).
- The final acceptance re-ran the exact command from a `git archive HEAD` copy: rc 0, the `.deb`,
  `.dsc` and `.tar.xz` in the parent (plus the unpublished `dbgsym`/`.buildinfo`/`.changes`);
  package contents and `Depends` as designed; `dpkg -i`/`-L`/`desktop-file-validate`/
  `xvfb-run -a qtranslate --version` (`QTranslate 0.12.0`)/`dpkg -r` pass; `verify-deb.sh` 15/15
  and `--self-test` 4/4; Release builds without warnings and passes 45/45 under `xvfb-run -a`;
  `dpkg-source -S` excludes a `build/` sentinel and `-x` extracts the `.dsc`.
- The CI and release invariants hold (the trixie job is cited from stage 2); the documentation
  link, `+astra1` and manual-`.deb` gates pass; `git diff dfd6f2e..HEAD -- src tests
  CMakeLists.txt` is empty and the `qtranslate` map has only its journal note. Windows is not
  performed (no MSVC). No push, tag or release was made.
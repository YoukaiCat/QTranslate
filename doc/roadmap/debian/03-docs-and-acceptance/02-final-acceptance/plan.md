---
kind: task
status: in-progress
detailed: true
depends: [01-docs-native-package]
---

# Final acceptance of the native Debian package

## Task

The final acceptance of this map. Task [`01-docs-native-package`](../01-docs-native-package/plan.md)
closed the documentation gap, stages 1 and 2 closed the package and the release integration, and
the lead has committed the map. This task checks the root `## Done when`
([`../../plan.md`](../../plan.md)) and the stage `## Done when` ([`../plan.md`](../plan.md)) on the
final tree, not on the intermediate commits, and records the evidence. It is an acceptance, not new
work: the normal result is a clean run with no source change.

Read first: the root `## Done when` (eight bullets), the stage `## Done when` (four bullets), the
stage-1 and stage-2 `## Outcome` sections (they hold the recorded runs and the deferred items), and
the journal entries of 2026-11-14.

Facts known before the run (from the stage outcomes and the journal, reused as recorded evidence
and to be cited as such): the trixie `debian:trixie` CI job was reproduced locally in stage 2
(`2e560e7..479264f`) and is not required to run again here; `dpkg-checkbuilddeps` is clean on
Astra; `parallel=N` reaches `ninja -jN` (stage 1); the trixie package builds with
`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`, and the gate is 15/15 there, with one explicit
`SELFTEST SKIP` in the self-test.

## Steps

Preconditions: `git status --porcelain` is empty; `git log --oneline -6` shows the documentation
commit and the map commit on top of `b9a6f75`; the tree is `/home/parsee/sync/mine/QTranslate`; the
host is Astra Linux 1.8.7 with `sudo -n`, `xvfb-run`, `desktop-file-validate`, `fakeroot`,
`lintian`, `ruby`, and a populated `build/linux-release` from earlier work (do not reuse stale
artifacts as evidence). If the tree is dirty, stop and report: the lead commits the map, not the
coordinator.

1. **Set up the acceptance workspace.** Build from a `git archive` copy, never from the working
   tree (`dpkg-buildpackage` writes to the parent and runs `debian/rules clean`):

   ```bash
   W=/tmp/opencode/accept-03
   S=/tmp/opencode/accept-03-source
   rm -rf "$W" "$S" && mkdir -p "$W/src" "$S/src"
   cd /home/parsee/sync/mine/QTranslate
   git archive HEAD | tar -x -C "$W/src"
   git archive HEAD | tar -x -C "$S/src"
   git status --porcelain          # empty
   git log --oneline -6
   ```

2. **Check the `debian/` layout and the version.** From the archive copy:

   ```bash
   cd "$W/src"
   ls -l debian debian/source
   test -x debian/rules && echo rules-executable
   dpkg-parsechangelog -S Version          # 0.12.0
   grep -n 'project(qtranslate VERSION' CMakeLists.txt    # 0.12.0
   cat debian/source/format                # 3.0 (native)
   cat debian/source/options               # bare tar-ignore, build, build-*, out
   sed -n '1,12p' debian/copyright         # DEP-5, "all rights reserved"
   dpkg-checkbuilddeps                     # silent, rc 0
   apt-get -s build-dep .                  # simulation; rc 0 on this host, 0 new packages
   ```

   The layout must have `control`, `rules`, `changelog`, `copyright`, `source/format`,
   `source/options`, `.gitignore` and `README.Debian`; `debian/source/format` is `3.0 (native)`;
   the version equals CMake's.

3. **Build with the exact user command** (no trailing `.`):

   ```bash
   cd "$W/src"
   DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc \
     > "$W/build.log" 2>&1
   echo "rc=$?"
   grep -n 'QTRANSLATE_BUILD_TESTS=OFF' "$W/build.log"    # the configure line
   grep -c 'Test #' "$W/build.log"                        # 0: nocheck skips the suite
                                                          # (rc 1 on a zero count)
   ls -1 "$W"    # in the parent, here: qtranslate_0.12.0_amd64.deb, qtranslate-dbgsym_*,
                 # qtranslate_0.12.0.dsc, qtranslate_0.12.0.tar.xz, *.buildinfo, *.changes
   ```

   Expect rc 0 and `qtranslate_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc`,
   `qtranslate_0.12.0.tar.xz` in `$W` (the parent of `$W/src`); the `dbgsym`, `.buildinfo` and
   `.changes` are expected there and are not published. Redirecting the output does not change the
   command; the 45 tests are a separate step 7.

4. **Check the package content and metadata.**

   ```bash
   dpkg-deb --info "$W"/qtranslate_0.12.0_amd64.deb
   dpkg-deb --contents "$W"/qtranslate_0.12.0_amd64.deb
   ```

   Required: `usr/bin/qtranslate`, `usr/share/applications/qtranslate.desktop`,
   `usr/share/icons/hicolor/256x256/apps/qtranslate.png`, `usr/share/doc/qtranslate/copyright`,
   `usr/share/doc/qtranslate/changelog.gz`; no `usr/share/doc/qtranslate/README.Debian` (the
   command has `nodoc`); no `usr/bin/qt.conf`, `usr/share/qt6`, `usr/lib`, `usr/plugins` or
   `usr/translations`; no `preinst`/`postinst`/`prerm`/`postrm`. `Depends` comes from
   `dh_shlibdeps`: it contains `libglib2.0-0` (not `libglib2.0-0t64`) and `libsecret-1-0` on this
   host, and no library that the binary does not link. `Version: 0.12.0`, `Architecture: amd64`,
   `Maintainer` as in `debian/control`.

5. **Install, run, remove.**

   ```bash
   sudo dpkg -i "$W"/qtranslate_0.12.0_amd64.deb
   dpkg -L qtranslate | grep -E '/(qtranslate|qtranslate\.desktop|qtranslate\.png)$'
   desktop-file-validate /usr/share/applications/qtranslate.desktop
   xvfb-run -a qtranslate --version          # QTranslate 0.12.0
   sudo dpkg -r qtranslate
   dpkg -s qtranslate 2>&1 | head -2         # not installed
   test ! -e /usr/bin/qtranslate && echo removed
   ```

   Do this before the gate: the gate's install roundtrip skips a package that is already installed
   or held.

6. **Run the gate on the produced `.deb`.**

   ```bash
   W=/tmp/opencode/accept-03
   "$W/src/packaging/linux/verify-deb.sh" "$W"/qtranslate_0.12.0_amd64.deb
   "$W/src/packaging/linux/verify-deb.sh" --self-test "$W"/qtranslate_0.12.0_amd64.deb
   ```

   On Astra expect 15/15 checks and a 4/4 self-test (the harness may need `sudo -n`; the gate
   states its own requirements). The gate already re-runs the install roundtrip,
   `dpkg-checkbuilddeps` and the `apt-get -s build-dep .` simulation; record their results as part
   of the 15. A trixie `SELFTEST SKIP` is not expected here and would be a finding.

7. **Run the Linux test suite on the final tree.**

   ```bash
   cmake -S "$W/src" -B "$W/build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build "$W/build-tests"
   xvfb-run -a ctest --test-dir "$W/build-tests" --output-on-failure
   ```

   Expect 45/45 and no build warnings. `nocheck`/`nodoc`/`parallel` were verified in stage 1
   (`debian/rules:4-31`; `parallel=N` reaches `ninja -jN`); cite that outcome for the `parallel`
   item and the `nodoc`/`nocheck` behaviour instead of re-running the whole build three ways. Do
   not use `build/linux-release` as the only evidence: it predates the final tree.

8. **Check the source package.**

   ```bash
   S=/tmp/opencode/accept-03-source
   cd "$S/src" && mkdir -p build && touch build/sentinel
   dpkg-buildpackage -S -us -uc
   ls -1 "$S"        # qtranslate_0.12.0.dsc, qtranslate_0.12.0.tar.xz, *_source.changes, *.buildinfo
   tar -tJf "$S"/qtranslate_0.12.0.tar.xz | grep -c '^qtranslate-0.12.0/build/'   # 0
   tar -tJf "$S"/qtranslate_0.12.0.tar.xz | grep -c '^qtranslate-0.12.0/\.git/'   # 0 (no .git in an archive copy)
   dpkg-source -x "$S"/qtranslate_0.12.0.dsc "$S/extracted"
   ls "$S/extracted/debian/source"
   ```

   `-S` builds, the `build/` sentinel stays out of the tarball, and `dpkg-source -x` extracts the
   `.dsc` (the extracted `debian/source/format` is `3.0 (native)`). `dpkg-buildpackage -S` may
   print the known lintian/info notes; only a failure is a finding.

9. **Check the CI and release invariants.**

   ```bash
   cd /home/parsee/sync/mine/QTranslate
   ruby -ryaml -e '
     wf = YAML.load_file(".github/workflows/build.yml")
     p wf["jobs"].keys
     p wf["jobs"]["linux"]["steps"].map { |s| s["name"] }
     p wf["jobs"]["linux"]["steps"].any? { |s| (s["run"] || "").include?("verify-deb.sh") }
     p wf["jobs"]["release"]["needs"]
     p wf["jobs"]["linux"]["steps"].map { |s| s["with"] }.compact.any? { |w| w["include-hidden-files"] == false }
   '
   grep -nE '^(debhelper|fakeroot|pkg-config|dpkg-dev)$' packaging/linux/trixie-packages.txt
   ```

   Expected: the YAML parses; the `linux` job installs the shared list (with the four packages),
   builds, runs `ctest`, runs `package.sh` and the gate, uploads `qtranslate-linux-x64` with
   `include-hidden-files: false`; the `release` job has `needs: [windows, linux]` and runs on
   `refs/tags/v*`; the artifact set is the tar.gz, `.deb`, AppImage and `SHA256SUMS-linux.txt`
   (plus the Astra set locally). Do not run the container job again; cite stage 2's recorded run
   (`0794258`, `010d882`, `479264f`).

10. **Check the documentation on the final tree.** Re-run the gates of
    `01-docs-native-package`, steps 7 and 8 (`astra1`, `build_deb`/`DEBIAN/control`/
    `dpkg-deb --build`, the exact command without `.`, the relative links) over the committed tree,
    and check the `debian` line in `doc/roadmap/README.md`. Then check that no document claims a
    Windows run for this change
    (`grep -n -i windows` in the new handoff section and the new architecture text) and that
    `debian/README.Debian` states the trusted-client behaviour measured under Xvfb (no entries
    needed there).

11. **Check the scope and the history.**

   ```bash
   cd /home/parsee/sync/mine/QTranslate
   git diff --stat dfd6f2e..HEAD -- src tests CMakeLists.txt      # empty
   git diff --stat b9a6f75..HEAD                                  # docs and the map only
   git diff --stat dfd6f2e..HEAD -- doc/roadmap/qtranslate         # one journal line
   grep -n 'status:' doc/roadmap/qtranslate/plan.md               # in-progress
   git status --porcelain                                         # empty
   ```

   The application code, the tests and `CMakeLists.txt` are untouched for the whole map
   (`dfd6f2e..HEAD`); the `qtranslate` map has only the one journal line and stays `in-progress`
   with `06-llm-actions` untouched; stage 3's own diff is documentation (the map's plan/journal
   plus the six documents). `packaging/linux/package.sh`, `publish`/release files and `debian/`
   are stage 1/2 scope and must not have changed in stage 3.

12. **Write the outcome.** In this task's `## Outcome` (the coordinator writes it; the planner does
    not), copy the root `## Done when` bullets and the stage `## Done when` bullets and give each
    one a line: the command, the result, and whether it was re-run now or cited from a stage
    outcome. Record explicitly:
    - the exact HEAD and the commit list `dfd6f2e..HEAD`;
    - the Astra run: rc 0, the artifacts, the package contents/`Depends`, the install/remove, the
      gate 15/15 and the self-test 4/4;
    - the 45/45 tests and the source-package checks;
    - the CI YAML invariants and the cited trixie run;
    - **Windows not performed** (no MSVC on this host): the 39 Windows tests and the installer are
      not part of this task, and no document may claim otherwise;
    - no push, no tag, no release. The outcome itself is this task's commit; the lead commits the
      map and closes it.

13. **Defect policy.** If a check fails, fix the defect minimally (a documentation or metadata
    line, or a `debian/`/packaging fix only if the stage-1/2 scope allows it without changing the
    interface), commit it once, and re-run the affected check. If the fix changes the root's goal,
    the map's scope, the version, the artifact name, the license, or needs a user decision, stop
    and record `НЕ УКЛАДЫВАЕТСЯ В ЗАДАЧУ` with the failing criterion and the options. Do not push,
    tag or release in any case; the lead closes the stage and the root and flips
    `doc/roadmap/README.md`.

## Done when

- The task's `## Outcome` has a row for every root `## Done when` bullet and every stage
  `## Done when` bullet, each with the command and the result, or the cited stage-1/2 outcome when
  the item was not re-run.
- From a `git archive HEAD` copy, `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc"
  dpkg-buildpackage -rfakeroot -us -uc` (no trailing `.`) exits 0 and writes
  `qtranslate_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc` and `qtranslate_0.12.0.tar.xz` (plus the
  unpublished `dbgsym`/`.buildinfo`/`.changes`) to the parent directory.
- `dpkg-deb --contents`/`--info` show the three application files and `copyright` +
  `changelog.gz` with no `README.Debian` (the command has `nodoc`), no bundle files, no maintainer
  scripts; `Depends` is `dh_shlibdeps` output with `libglib2.0-0` and `libsecret-1-0` on Astra.
- `dpkg -i`, `dpkg -L`, `desktop-file-validate`, `xvfb-run -a qtranslate --version`
  (`QTranslate 0.12.0`) and `dpkg -r` pass with no leftovers.
- `packaging/linux/verify-deb.sh` reports 15/15 and `--self-test` 4/4 on Astra.
- The final tree builds Release and passes 45/45 Linux tests under `xvfb-run -a`; `nocheck`,
  `nodoc` and `parallel` are accounted for from stage 1's recorded run, not contradicted.
- `dpkg-checkbuilddeps` is silent; `dpkg-source -S` and `dpkg-source -x` work on the final tree
  (`-b`/`-tc` are cited from stage 1), and a `build/` sentinel stays out of the tarball.
- The CI YAML parses; the Linux job keeps the native build, the gate, `ctest`, `qtranslate-linux-x64`
  and `include-hidden-files: false`; the `release` job keeps `needs: [windows, linux]`.
- The documentation gates of `01-docs-native-package` pass on the committed tree: the exact command
  with no trailing `.`, no current `+astra1`, no manual `.deb` staging claim, the relative links
  resolve.
- `git diff --stat dfd6f2e..HEAD -- src tests CMakeLists.txt` is empty; `doc/roadmap/qtranslate`
  has only the one journal line; before the outcome is written, `git status --porcelain` is empty.
- The outcome records Windows as not performed, and no push, tag or release was made.
- The task's plan with the `## Outcome` is committed as the coordinator's task commit; the lead
  commits the map; a defect fix would be one minimal extra commit and its check re-run.

## Risks

- **Acceptance instead of re-implementation.** Stage 1 and 2 already ran the expensive checks; the
  task is to prove they still hold on the final tree, not to redo the trixie container job or the
  three-way `nocheck`/`nodoc`/`parallel` matrix. Cite the recorded runs and re-run the items the
  root lists literally.
- **Stale local artifacts.** `build/artifacts-linux` still holds the 0.11.0 files (and the old
  `+astra1` `.deb`); never pick a `.deb` from there. Build into `/tmp/opencode/accept-03`.
- **The install roundtrip and the gate share the system package.** Remove the manual install
  before the gate, or the gate's roundtrip silently skips.
- **The map status.** `doc/roadmap/README.md` says `debian — in-progress` during this task; that is
  correct. The status flip and the journal entry are the lead's closing actions, not findings.
- **The stage's closing wording.** The stage's `## Done when` names a commit and a push; read the
  first as no change to the application, packaging, CI or documentation. The acceptance's own
  commit carries the `## Outcome` (the coordinator commits the task), and the lead commits the map;
  a defect fix is one minimal extra commit, and the lead records it in the journal.
- **Windows.** There is no MSVC on this host; never write that the Windows build or the 39 tests
  were verified, and never copy a "passed" from the Linux results.

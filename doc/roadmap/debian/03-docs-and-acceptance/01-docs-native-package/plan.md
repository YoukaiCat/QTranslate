---
kind: task
status: done
detailed: true
---

# Document the native Debian package

## Task

The lead has already edited the live documents for the native package in the working tree:
`README.md`, `README.ru.md`, `doc/architecture.md`, `doc/handoff.md` and `AGENTS.md` no longer name
`+astra1`, and they describe the top-level `debian/` source package, the exact `dpkg-buildpackage`
command without a trailing `.`, the `nocheck`/`nodoc`/`parallel` options, the
`packaging/linux/verify-deb.sh` gate, and the native `.deb` in CI and the release. The edits are
not committed. This task is the review and finish pass over them: check every claim against the
tree, fix what the edits left behind, and close the link and EN/RU gaps. Do not rewrite the parts
that are already right.

Why it is needed: the stage's `## Done when` requires that no live document describes a manual
`.deb` build and that the native package is documented; the root `## Done when` requires the same
for the five documents and `doc/roadmap/README.md`. The acceptance (task
[`02-final-acceptance`](../02-final-acceptance/plan.md)) verifies the final tree, so this task must
leave nothing for it to find.

Scope: documentation only. The files are `README.md`, `README.ru.md`, `AGENTS.md`,
`doc/architecture.md`, `doc/handoff.md`, `debian/README.Debian`, and the one-line description in
`doc/roadmap/README.md`. Do not edit `doc/roadmap/qtranslate/**` (the archived map), do not touch
the `qtranslate` map nodes, and do not change the `debian` map status in `doc/roadmap/README.md`:
the lead sets it to `done` when the root closes.

## Steps

Facts checked while planning on 2026-11-14 (Astra Linux 1.8.7, HEAD `b9a6f75`; the five document
edits are uncommitted):

- `grep -n astra1` in the live documents returns only `doc/handoff.md:77` and `:135` — "the
  `+astra1` suffix is gone / was dropped". That is the historical record of the decision and stays;
  it is not a package name.
- `doc/roadmap/README.md:4` still says `in-progress`, which is correct until the root closes.
- `doc/architecture.md` has the `debian/` node in the root layout (`:139-158`), but the
  `doc/roadmap/` subtree (`:173-185`) does not list `debian/`, and the `releases/` list (`:168-172`)
  stops at `0.11.0.md` although `doc/releases/0.12.0.md` exists.
- `debian/README.Debian:30-41` says the passive-grab tests in the Xvfb build need the
  trusted-client entries, while the stage-1 outcome records 45/45 under Xvfb without them and
  `doc/handoff.md:125` says the same. The two documents contradict each other.
- `README.md:244-248` and `README.ru.md:246-250` name only `.deb`, `.dsc` and `.tar.xz` in the
  parent directory; `dpkg-buildpackage` also writes `dbgsym`, `.buildinfo` and `.changes` there
  (`debian/README.Debian:12-21` lists all six), and the user decided the `dbgsym` is not published.
- `grep -nE 'build_deb|DEBIAN/control|dpkg-deb --build'` over those six files is already empty.
  `dpkg-shlibdeps` occurs once (`doc/handoff.md:130`) in the sentence that explains the gate's
  expected `Depends`; that is not a manual staging claim and stays. The remaining work is
  completeness, not removal.

1. **`README.md` and `README.ru.md`.** The Linux section (`README.md:182-273`,
   `README.ru.md:186-275`), the Astra download paragraph (`README.md:70-75`,
   `README.ru.md:71-75`) and the known-limitations bullet (`README.md:308-309`,
   `README.ru.md:311-312`). Check, and fix in both:
   - The command is exactly
     `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
     (`README.md:241`, `README.ru.md:243`), no trailing `.`, and the prose says why (Astra's
     `dpkg-buildpackage` 1.21 rejects `.`, and `.` is not an argument). Do not reorder `-us -uc`.
   - The artifact sentence after the code block (`README.md:244-248`, `README.ru.md:246-250`)
     must also name the `dbgsym`, `.buildinfo` and `.changes` files and say they are not
     published. Today it reads as if the parent directory held exactly three files.
   - The Astra names are `qtranslate-<version>-linux-x86_64-astra.tar.gz` and
     `qtranslate_<version>_amd64.deb` (same file name as the trixie package, built against Astra's
     Qt 6.10.2 / glibc 2.36), plus `SHA256SUMS-linux-astra.txt`; no `+astra1` anywhere.
   - `nocheck` skips 45 tests, `nodoc` leaves `README.Debian` out, `parallel=N` bounds the build;
     the build dependencies live in `debian/control` and `dpkg-checkbuilddeps` reports the gaps.
   - The internal anchor `#linux-0120` still matches the heading `### Linux (0.12.0)` in both
     files.
   - The "Astra Linux: global hotkeys" subsection stays about the installed binary
     (`/usr/bin/qtranslate(KBD_R)`) and must not be mixed with the build-time trusted-client entry
     of `debian/README.Debian`.

2. **`AGENTS.md`.** The Build and test bullet (`:29-35`) and the CI bullet (`:88-95`). Verify that
   the command, the "no trailing `.`" note, the native source package, the `.deb` built by
   `package.sh` through `dpkg-buildpackage`, `packaging/linux/verify-deb.sh`,
   `debhelper`/`fakeroot`/`pkg-config`, `include-hidden-files: false`, the 45 Linux tests and the
   `qtranslate-linux-x64` upload are stated once and consistently. The file is instructions for
   agents: keep it imperative and free of history. Do not add a Jira key and do not duplicate the
   README text.

3. **`doc/architecture.md`.** Four places:
   - the file/layout list: the `debian/` node and `verify-deb.sh` (`:139-158`); add `debian/` to
     the `doc/roadmap/` subtree (`:173-185`) and `0.12.0.md` to the `releases/` list (`:168-172`).
     Both are one-line fixes of stale lists in the file this task already touches.
   - §3.4 (`:376-410`): `0.12.0` in the version paragraph, `debian/changelog` and the tag; the
     native source package, the exact command, the package contents and `Depends` from
     `dh_shlibdeps`, the gate; the `.deb` from `package.sh` in a clean copy; the Astra `.deb`
     without the suffix and the shared file name; the unpublished `dbgsym`/`.changes`/`.buildinfo`.
   - the CI section (`:440-450`): the package list with `debhelper`/`fakeroot`/`pkg-config`, the
     native build through `package.sh`, the `verify-deb.sh` step, `include-hidden-files: false`.
   - §9 "Build and packages" (`:1924-1932`): the top-level `debian/`, the native `.deb`, the Astra
     `.deb` name.
   The `0.11.0` hits in §8.4 (`:2306-2327`) are the historical release verification and stay
   untouched; do not reflow unrelated paragraphs.

4. **`doc/handoff.md`.** The date and the intro (`:1-8`), the Packaging and CI bullets (`:71-86`),
   and the new "Native Debian packaging (2026-11-14)" section (`:108-143`). Check the facts against
   the tree (`debian/` contents, `0.12.0`, the two X11 test names, `15` checks, `45/45`,
   `debhelper` 13.11.4 on Astra, `13.16` in trixie, the removed `+astra1`). One correction is
   needed: `:118` says the map was accepted "over `dfd6f2e..479264f`", but `b9a6f75` closed the
   stage and the docs commit comes after it. Make the sentence true for the final HEAD (or drop
   the precise range and keep the date); the lead's closing edit may set the last commit. Keep the
   two `+astra1`-removal statements. Do not recompute the old `0.11.0` hashes, release notes or
   history sections.

5. **`debian/README.Debian`.** The command and the "no trailing `.`" paragraph (`:1-10`), the
   parent-directory list and the `dbgsym` sentence (`:12-21`), the `DEB_BUILD_OPTIONS` block
   (`:23-28`), the trusted-client paragraph (`:30-41`), the source-package section (`:43-69`).
   - Reword the trusted-client paragraph so it matches the measurement: `debian/rules` runs the
     suite under `xvfb-run -a`, and that passed 45/45 without the entries; the entries are the
     fallback when the X server enforces XPARSEC (a real Parsec session). Keep the exact
     `/etc/X11/trusted.d/qtranslate` form, both `tst_hotkeymanager_x11` and
     `tst_hotkey_selection_x11` paths, and `sudo /usr/bin/fly-set-execid.sh`.
   - State that `qtranslate-dbgsym` is built but not published (the user's decision), next to the
     existing "the debug symbols are in `qtranslate-dbgsym`" sentence.
   - Confirm the `nocheck`/`nodoc`/`parallel` claims against `debian/rules:4-31` (`nocheck` sets
     `QTRANSLATE_BUILD_TESTS=OFF`; `nodoc` excludes `README.Debian` and keeps `copyright` and the
     changelog) and the `0.12.0` file names against `debian/changelog`.
   - Do not change the technical content of the source-package section; it was verified in stage 1.

6. **`doc/roadmap/README.md`.** Line 4 only: the `debian` description must name the native
   `debian/` package built with `dpkg-buildpackage` and the release/CI integration. The status
   stays `in-progress` here (the lead flips it when the root closes — record that in the report so
   it is not lost), and the `qtranslate` line is unchanged.

7. **Run the text gates.** From the repository root:

   ```bash
   cd /home/parsee/sync/mine/QTranslate
   grep -nE 'astra1' README.md README.ru.md AGENTS.md doc/architecture.md doc/handoff.md \
       debian/README.Debian
   # only doc/handoff.md:77 and :135, both saying the suffix is gone
   grep -nE 'build_deb|DEBIAN/control|dpkg-deb --build|stages the CMake install tree' \
       README.md README.ru.md AGENTS.md doc/architecture.md doc/handoff.md debian/README.Debian
   # empty; "not from a hand-staged tree" is the intended negation and may stay
   grep -n 'dpkg-buildpackage -rfakeroot -us -uc' README.md README.ru.md AGENTS.md \
       doc/architecture.md doc/handoff.md debian/README.Debian
   # six hits; none followed by a " ." (the command has no trailing dot)
   grep -n 'us -uc \.' README.md README.ru.md AGENTS.md doc/architecture.md doc/handoff.md \
       debian/README.Debian
   # empty
   ```

8. **Check the relative links.** Every relative link in the touched documents must resolve; the
   new `roadmap/debian/plan.md` link in `doc/handoff.md` is the one most likely broken. Heuristic
   (anchors and URLs are skipped; the two `#linux-0120` anchors are checked by hand in step 1):

   ```bash
   cd /home/parsee/sync/mine/QTranslate
   for f in README.md README.ru.md AGENTS.md doc/*.md doc/roadmap/README.md \
            doc/roadmap/debian/plan.md; do
     d=$(dirname "$f")
     grep -oE '\]\([^)]+\)' "$f" | sed -E 's/^\]\(//; s/\)$//' \
       | grep -vE '^(https?://|mailto:|#)' | while IFS= read -r l; do
           p=${l%%#*}
           [ -e "$d/$p" ] || echo "MISSING $f -> $l"
         done
   done
   # empty output
   ```

9. **Check the EN/RU sync.** The READMEs are translations of each other and this change adds
   facts to both, so compare them fact by fact rather than reading them:

   ```bash
   cd /home/parsee/sync/mine/QTranslate
   grep -c 'DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc' README.md README.ru.md   # 1 1
   grep -c 'qtranslate_<version>_amd64.deb' README.md README.ru.md                                                          # 1 1
   grep -c 'SHA256SUMS-linux-astra.txt' README.md README.ru.md                                                              # 1 1
   grep -c 'dbgsym' README.md README.ru.md                                                                                  # 1 1 after step 1
   grep -n '^### Linux (0.12.0)' README.md README.ru.md                                                                     # 1 1
   ```

   The section headings may be translated, the code blocks, the command, the file names and the
   numbers may not differ. Do not machine-translate a block that is already correct.

10. **Commit.** The lead commits these documentation files as one commit, separate from the map
    commit (the stage plan, the task plans and the journal), and does not push. `debian/README.Debian`
    travels with the documentation commit because it is documentation, not packaging behavior.

## Done when

- The exact command
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  appears in `README.md`, `README.ru.md`, `AGENTS.md`, `doc/architecture.md`, `doc/handoff.md` and
  `debian/README.Debian`; `grep -n 'us -uc \.'` over them is empty, and every file says why the dot
  is absent.
- `build_deb`, `DEBIAN/control` and `dpkg-deb --build` are absent from the six live documents; no
  document claims that `packaging/linux/package.sh` stages the `.deb` by hand. The single
  `dpkg-shlibdeps` mention (`doc/handoff.md:130`) is the gate's own description and stays.
- `astra1` appears only in the two "the suffix is gone/was dropped" sentences of
  `doc/handoff.md`; no document names a `+astra1` package as current.
- The parent-directory outputs are documented as `.deb`, `.dsc`, `.tar.xz`, `dbgsym`, `.buildinfo`
  and `.changes`, the `dbgsym` is stated as not published (READMEs and architecture/handoff), and
  `debian/README.Debian` says the debug symbols are a separate package.
- The Astra artifact names are `qtranslate-<version>-linux-x86_64-astra.tar.gz`,
  `qtranslate_<version>_amd64.deb` and `SHA256SUMS-linux-astra.txt`; the CI upload is
  `qtranslate-linux-x64`; no document gives a different name.
- The Astra trusted-client step is documented for the installed binary in both READMEs and for the
  build tests in `debian/README.Debian`, and no document says the Xvfb build needed it.
- Every relative link in the touched documents resolves (the step-8 loop prints nothing), and
  `#linux-0120` matches `### Linux (0.12.0)` in both READMEs.
- The EN and RU READMEs state the same facts: the command, the artifact and option names, the
  numbers, the Astra names; the counts in the step-9 greps match.
- `doc/architecture.md` lists `debian/` in the root layout and in the `doc/roadmap/` subtree,
  `0.12.0.md` in `releases/`, and §3.4/§9/CI agree with the tree; §8.4 keeps its historical
  `0.11.0` facts.
- `doc/handoff.md` has the 2026-11-14 date, the new section and a true acceptance range for the
  final HEAD.
- `doc/roadmap/README.md` line 4 describes the native package; the `debian` status is still
  `in-progress` and the `qtranslate` line is unchanged.
- `git status --short` shows only documentation: the six files above plus the map's own plan and
  journal edits; no `src/`, `tests/`, `CMakeLists.txt`, `packaging/`, `debian/rules|control|
  changelog|copyright|source/` change.
- The lead committed the documentation as one commit; no push.

## Risks

- **Editing the history sections.** `doc/handoff.md` records the published `0.11.0`/`0.11.1`
  releases and the `+astra1` decision; `doc/architecture.md` §8.4 is the 0.11.0 verification.
  Keep them; only the current-state text changes.
- **`debian/README.Debian` is the one non-`doc/` file.** It is documentation and belongs to this
  task, but it is also read by the packager: do not soften the exact command, the file names or
  the trusted-client paths while rewording the paragraph.
- **The READMEs are a translation pair.** A fact added to one must be added to the other in the
  same commit; a changed anchor must be fixed in both.
- **Do not add the gate to the README.** `verify-deb.sh` is a development/CI gate; `AGENTS.md` and
  `doc/architecture.md` already document it, and the README is for builders and users.
- **Do not touch the map status or the `qtranslate` map.** The status flip is the lead's action
  when the root closes, and the archive is read-only.

## Outcome

Done by the lead as a documentation-only task; committed as `c7e5808` (six files, no code).

- `README.md` and `README.ru.md` now document the top-level `debian/` source package and the exact
  command `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  with no trailing dot and the reason; the parent directory is described as holding the `.deb`,
  `.dsc`, `.tar.xz`, `dbgsym`, `.buildinfo` and `.changes`, with the `dbgsym` not published; the
  Astra names are `-astra.tar.gz`, `qtranslate_<version>_amd64.deb` and
  `SHA256SUMS-linux-astra.txt`; `nocheck`/`nodoc`/`parallel` and `debian/control` are explained.
  The download paragraph and the known-limitations bullet no longer mention `+astra1`.
- `AGENTS.md` states the native source package, the command, `verify-deb.sh`, the
  `debhelper`/`fakeroot`/`pkg-config` CI additions and `include-hidden-files: false`.
- `doc/architecture.md` has `debian/` in the root layout and in the `doc/roadmap/` subtree,
  `0.12.0.md` in the `releases/` list, and updated §3.4, CI and §9 sections: the native package and
  its contents, `Depends` from `dh_shlibdeps`, the `.deb` built by `package.sh` in a clean copy, the
  gate, and the Astra `.deb` without the suffix.
- `doc/handoff.md` is dated 2026-11-14, lists both active roadmaps, has a "Native Debian packaging
  (2026-11-14)" section with the command, options, gate results and known gaps, and no longer
  claims a specific acceptance hash for the open stage.
- `debian/README.Debian` says the Xvfb run passed 45/45 without trusted-client entries and that the
  `/etc/X11/trusted.d/qtranslate` setup is the fallback for a real Parsec server; it also states
  that `qtranslate-dbgsym` is built but not published.
- `doc/roadmap/README.md` line for `debian` already described the native package and is unchanged;
  its status flip to `done` is the lead's closing edit.

Verification: `astra1` appears only in the two `doc/handoff.md` sentences that record the suffix
removal; `build_deb`, `DEBIAN/control` and `dpkg-deb --build` are absent from the six documents;
the exact command appears once in each of the six; no `us -uc .`; every relative link resolves; the
EN/RU counts for `dbgsym`, the Astra `.deb` name and `SHA256SUMS-linux-astra.txt` match; the
`### Linux (0.12.0)` anchor matches `#linux-0120` in both READMEs.

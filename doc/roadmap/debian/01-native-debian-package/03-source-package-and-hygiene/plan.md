---
kind: task
status: done
detailed: true
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
  `build`, `build-*`, and `out`, for the local `.claude` directory, and — added by this task — for
  `.git`. The options are shown by `dpkg-source --build` and are effective: a
  `build/` sentinel, a `build-local/` sentinel, an `out/` sentinel, a `.claude/` sentinel, and a real
  `.git` directory must not appear in the `.tar.xz`.
- `debian/changelog` has the native version `0.12.0` with no Debian revision;
  `dpkg-parsechangelog -S Version` equals `project(qtranslate VERSION ...)` in `CMakeLists.txt`;
  `debian/copyright` is DEP-5 and states all rights reserved.
- `debian/.gitignore` covers the generated debhelper files (`debian/files`,
  `debian/*.substvars`, `debian/qtranslate.debhelper.log`, `debian/debhelper-build-stamp`,
  `debian/.debhelper/`, `debian/qtranslate/`); after a build `git status` shows only the tracked
  `debian/` sources.
- A clean-copy build: copy the tree without `build` and `out` (but with `.git`, to prove the
  exclusion); run `dpkg-source -b` and inspect the tarball; run `dpkg-source -x` on the `.dsc`; run
  `dpkg-buildpackage -S` and `dpkg-buildpackage -b` from the extracted source; run
  `dpkg-buildpackage -tc` and confirm the post-clean.
- `debian/rules clean` removes `build/debian`, `debian/files`, `debian/qtranslate/`, and the
  generated `debian/*.substvars`; a second build from the same tree starts clean.
- The source package does not include `packaging/linux` build outputs, `build/linux-tools`, or
  the AppImage cache; only the tracked packaging sources.
- The `debian/README.Debian` build instructions match the actual command and options.

Measured while planning (Astra Linux 1.8.7, `dpkg-source` 1.21.22, `debhelper` 13.11.4+b7), and the
reason the first bullet above grew a `.git` entry:

- **`dpkg-source` ships `.git` in a `3.0 (native)` tarball once `debian/source/options` names any
  `tar-ignore` pattern.** A throwaway `git init` tree built with `dpkg-source -b` and no options
  file had no `.git` entries (the default `-I` list applies), but the same tree with only
  `tar-ignore = build` and friends produced `pkg/.git/HEAD`, `pkg/.git/index`,
  `pkg/.git/objects/…`: a `tar-ignore` line with a value **replaces** the default `-I` list instead
  of extending it. A bare `tar-ignore` line with no value adds the defaults back, so the options
  file keeps its four patterns and still excludes `.git`, `*.o`, `*.so`, `.svn`, and the rest.
  With `tar-ignore = .git` alone the probe lost `.git/` and kept `.gitignore` (both the root one and
  `debian/.gitignore`) — `dpkg-source` does not read `.gitignore`, so `.git` has to be covered
  explicitly, but a single `tar-ignore = .git` silently drops the other defaults.
- **`tar-ignore` matches a path component at any depth, not just the top level.** The same probe
  excluded `src/build/sentinel` via `tar-ignore = build` and kept `.github/workflows/build.yml`
  (matching is on the whole name, so `build` does not swallow `build.yml`; `build-*` does match
  `build-local` and `build-x`).
- **The measured residue of a `nocheck` build on this host** (debhelper 13.11.4, recorded from the
  build tree the task-01 coordinator left behind) is `build/debian/`, `debian/.debhelper/`,
  `debian/debhelper-build-stamp`, `debian/files`, `debian/qtranslate/`,
  `debian/qtranslate.debhelper.log`, `debian/qtranslate.substvars`. `debian/qtranslate-dbgsym/` is
  not created: the debug root lives in `debian/.debhelper/qtranslate/dbgsym-root`. The residue is
  already ignored: `build/` by the root `.gitignore`, the `debian/` paths by `debian/.gitignore`
  (`/qtranslate-dbgsym/` is a guard, see step 4).
- **`debian/rules clean` on a copy dirtied by a task-01 build exits 0 and leaves `debian/` with its
  eight tracked entries only**; `build/debian` is gone and, because nothing else was in `build/`, the
  empty `build/` disappeared too. A probe with a populated `build/` (`build/sentinel`,
  `build/linux-tools/tool`), `out/sentinel` and `.claude/sentinel` kept all four and removed only
  `build/debian` — the user's 4.1 GB `build/` is safe, and only an empty `build/` vanishes.
- **`lintian` on the task-01 `.dsc` reports no copyright or license tag**, so `debian/copyright` is
  complete DEP-5 and is not edited here. It reports one unrelated `E:
  build-depends-on-build-essential`; `debian/control` is out of scope for this task and the tag is
  recorded for stage 2/3, not fixed.

Already verified in task 01 (`8185ecc`), not repeated as new work: `debian/source/format` is
`3.0 (native)`, `tar-ignore` for `build`/`build-*`/`out`/`.claude` works on a sentinel tree,
`dpkg-source -x` extracts the `.dsc`, and `debian/changelog` is `0.12.0`. This task re-reads that
evidence once (`--print-format`, `tar -tf`) and spends the effort on `.git`, the generated-file
inventory, `clean`, `-tc`, and the extracted-source builds.

Not in scope: `debian/control`, `debian/changelog` (the version stays `0.12.0`), application code,
tests, `CMakeLists.txt`, `packaging/linux/package.sh`, `packaging/linux/verify-deb.sh`, and CI. The
only files expected to change are `debian/source/options`, `debian/README.Debian`,
`debian/.gitignore` (only if a pattern is missing), `debian/rules` (only if a clean check fails),
and this `plan.md`.

## Steps

Paths used below: `REPO=/home/parsee/sync/mine/QTranslate`,
`H=/tmp/opencode/qtranslate-hygiene` (copy with `.git`),
`S=/tmp/opencode/qtranslate-hygiene-sentinel` (copy with sentinels),
`B=/tmp/opencode/qtranslate-hygiene-build` (extracted source). Every copy and every artifact lives
under `/tmp/opencode`; nothing is built inside `REPO`. Run the shells with `set -o pipefail`, so a
`tee` does not hide a failing build. `nproc` is 24 on this host.

1. **Check the metadata in the repository (read-only, no build).** From `REPO`:

   ```
   dpkg-source --print-format .                 # must print: 3.0 (native)
   cat debian/source/format                     # exactly "3.0 (native)" and a newline
   dpkg-parsechangelog -S Version               # must print: 0.12.0, with no '-'
   dpkg-parsechangelog -S Source                # qtranslate; equals Source: in debian/control
   dpkg-parsechangelog -S Maintainer            # equals Maintainer: in debian/control
   grep -n '^project(qtranslate VERSION' CMakeLists.txt   # line 2: 0.12.0
   test -x debian/rules && stat -c '%a' debian/rules      # 755
   dpkg-checkbuilddeps                          # silent, exit 0
   ```

   Copyright (DEP-5 and all rights reserved, no license grant):

   ```
   head -1 debian/copyright                     # Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
   grep -q '^Files: \*$' debian/copyright
   grep -q '^Copyright: ' debian/copyright
   grep -qE '^License: All-Rights-Reserved$' debian/copyright
   grep -q 'All rights reserved\.' debian/copyright
   ```

   Success: every command above matches; a version with a `-` revision fails the task (the native
   format rejects it). `debian/copyright` is not edited: `lintian qtranslate_0.12.0.dsc` shows no
   copyright or license tag (measured). Also record the `git status --porcelain -uall` baseline: the
   lead's uncommitted `doc/roadmap/debian/journal.md` and this `plan.md`, nothing else.

2. **Restore `dpkg-source`'s default exclusions and keep the project ones.** A `tar-ignore` line
   with a value replaces the default `-I` list instead of adding to it, which is why the current
   four lines drop `-I.git` and would ship the whole `.git` in a native tarball. A bare
   `tar-ignore` line with no value adds the default list back. The file becomes:

   ```
   tar-ignore
   tar-ignore = build
   tar-ignore = build-*
   tar-ignore = out
   tar-ignore = .claude
   ```

   Measured on Astra with `dpkg-source` 1.21.22: the five lines above kept a probe tarball at 0
   `.git/` entries while still excluding `build/`, `build-local/`, and `out/`; the four pattern
   lines alone produced 2627 `.git/` entries. Do not replace the bare line with
   `tar-ignore = .git`: that excludes `.git` but silently drops `*.o`, `*.so`, `*.a`, `*.la`,
   `.svn`, and the rest of the defaults.

   No comment lines: the reason is recorded here, in `debian/README.Debian` (step 9), and in the
   task's `## Outcome`. Success is shown by the `--tar-ignore` (no value) in the `dpkg-source`
   output in step 3.

3. **Source tarball from a copy that has `.git` (cheap, no compile).**

   ```
   mkdir -p $H
   rsync -a --exclude build --exclude 'build-*' --exclude out --exclude .claude \
         $REPO/ $H/qtranslate-0.12.0/
   test -d $H/qtranslate-0.12.0/.git          # the copy carries the VCS metadata
   test ! -e $H/qtranslate-0.12.0/build
   cd $H
   dpkg-source -b qtranslate-0.12.0 2>&1 | tee $H/dpkg-source-before.log
   tar -tf qtranslate_0.12.0.tar.xz > $H/tar-before.txt
   ```

   Success:
   - `$H/dpkg-source-before.log` contains the option line
     `--tar-ignore --tar-ignore=build --tar-ignore=build-* --tar-ignore=out --tar-ignore=.claude`
     (the bare `--tar-ignore` re-adds the defaults);
   - `$H/qtranslate_0.12.0.dsc` and `$H/qtranslate_0.12.0.tar.xz` exist, and the tarball's top
     directory is `qtranslate-0.12.0/`;
   - `grep -E '(^|/)\.git(/|$)' $H/tar-before.txt` prints nothing (this is the new check: `.git`
     exists in the tree and is absent from the tarball);
   - `grep -qx 'qtranslate-0.12.0/CMakeLists.txt'`, `grep -qx 'qtranslate-0.12.0/debian/control'`,
     `grep -qx 'qtranslate-0.12.0/debian/source/options'`,
     `grep -qx 'qtranslate-0.12.0/packaging/linux/verify-deb.sh'` and
     `grep -qx 'qtranslate-0.12.0/src/ai/openaichat.cpp'` all succeed (the tarball is not empty and
     carries the tracked sources);
   - `grep -E '\.gitignore$' $H/tar-before.txt` prints nothing: the restored default `-I` list
     contains `.gitignore` and `.gitattributes`, so both `.gitignore` files (and the root
     `.gitattributes`) are absent from the tarball. That is expected; do not bring them back with a
     valued `tar-ignore`, which would drop `.git` and the other defaults again;
   - `grep -E '(^|/)build(/|$)' $H/tar-before.txt` prints nothing.

4. **Build once with the user's exact command, then inventory the generated files.**

   ```
   cd $H/qtranslate-0.12.0
   DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" \
       dpkg-buildpackage -rfakeroot -us -uc 2>&1 | tee $H/build1.log
   ```

   No trailing `.`. Success: exit 0; `$H` gains `qtranslate_0.12.0_amd64.deb`,
   `qtranslate-dbgsym_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc`, `qtranslate_0.12.0.tar.xz`,
   `qtranslate_0.12.0_amd64.buildinfo`, `qtranslate_0.12.0_amd64.changes`.

   Then inventory what the build left in the copy:

   ```
   git status --porcelain -uall
   git ls-files --others | sed -E 's#^(build/).*#\1...#; s#^debian/qtranslate/.*#debian/qtranslate/...#' | sort -u
   git status --ignored --porcelain -uall | sed -n 's/^!! //p' | wc -l
   ```

   The `sed` collapses the thousands of files under `build/` and `debian/qtranslate/` into one entry
   each, so the output is the short list that matters.

   Success:
   - `git status --porcelain -uall` has **no line starting with `??`**; the only ` M` lines are the
     tracked files this task edits (`debian/source/options`, `debian/README.Debian`, plus
     `debian/.gitignore` / `debian/rules` if a later check forces them) and the lead's
     `doc/roadmap/debian/journal.md` plus this `plan.md`;
   - the collapsed `git ls-files --others` list is exactly `build/...`, `debian/.debhelper/...`,
     `debian/debhelper-build-stamp`, `debian/files`, `debian/qtranslate/...`,
     `debian/qtranslate.debhelper.log`, `debian/qtranslate.substvars` (measured set; a
     `debian/qtranslate-dbgsym/...` entry would be possible if a debhelper version creates one);
   - anything outside that set is a gap: add a matching pattern to `debian/.gitignore` (or to the
     root `.gitignore` if the path is outside `debian/`), then re-run the three commands. No rebuild
     is needed to validate an ignore pattern.

   Expected outcome: the measured set is already ignored (`build/` by the root `.gitignore`, the
   `debian/` paths by `debian/.gitignore`), so no pattern is added; the entry `/qtranslate-dbgsym/`
   stays as a guard even though debhelper 13.11.4 keeps the debug root in
   `debian/.debhelper/qtranslate/dbgsym-root`. Do not add patterns for paths that did not appear.

5. **`debian/rules clean` on the dirty tree, then a second build.** First prove that the clean
   target removes everything the build left:

   ```
   cd $H/qtranslate-0.12.0
   debian/rules clean
   test ! -e build/debian
   test ! -e debian/files
   test ! -e debian/qtranslate
   test ! -e debian/.debhelper
   test ! -e debian/debhelper-build-stamp
   test -z "$(find debian -maxdepth 1 \( -name '*.substvars' -o -name '*.debhelper*' \) -print)"
   test -z "$(find build -mindepth 1 -print -quit 2>/dev/null)"   # build/ is gone or empty
   git status --porcelain -uall            # no '??' line
   git status --ignored --porcelain -uall  # no '!!' line: no generated file is left
   ```

   `build/debian` must go, and `build/` may go with it when it held nothing else (measured: the empty
   `build/` disappears; a populated one keeps its other content, see step 7). `debian/rules:33-35`
   deletes only `build/debian`, and the repository's `build/` is the user's 4.1 GB development build
   tree. Never widen that to `rm -rf build`.

   Then the second build from the same copy, now with post-clean, using the same exact options:

   ```
   DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" \
       dpkg-buildpackage -rfakeroot -us -uc -tc 2>&1 | tee $H/build2-tc.log
   ```

   No trailing `.`. Success: exit 0 with a clean start (pre-clean runs `debian/rules clean`), the same
   six artifacts in `$H`, and immediately after it the same checks as above pass again with the tree
   clean (`build/debian` gone, `git status -uall` without `??`, `git status --ignored` with no `!!`
   line). Then
   prove the clean target is idempotent: `debian/rules clean` again exits 0 and the tree is still
   clean.

6. **The cleaned tree builds the same source package.** From `$H`:

   ```
   dpkg-source -b qtranslate-0.12.0 2>&1 | tee $H/dpkg-source-after.log
   tar -tf qtranslate_0.12.0.tar.xz > $H/tar-after.txt
   diff $H/tar-before.txt $H/tar-after.txt
   ```

   Success: `diff` is empty — the build's residue never enters `qtranslate_0.12.0.tar.xz`, and the
   pre-build and post-`-tc` source packages carry the same path list. Do not run `dpkg-source -b`
   directly on a tree that still has debhelper residue: `tar-ignore` does not cover
   `debian/qtranslate/`, `debian/qtranslate.substvars` or `debian/debhelper-build-stamp`, so such a
   tarball would contain them (`debian/files` is excluded by `dpkg-source` itself); `debian/rules
   clean` (or `dpkg-buildpackage`, which pre-cleans) must run first.

7. **Sentinel copy for the `tar-ignore` patterns.**

   ```
   mkdir -p $S
   rsync -a --exclude .git --exclude build --exclude 'build-*' --exclude out --exclude .claude \
         $REPO/ $S/qtranslate-0.12.0/
   cd $S/qtranslate-0.12.0
   mkdir -p build build-local out .claude
   for d in build build-local out .claude; do echo sentinel > "$d/sentinel"; done
   cd $S
   dpkg-source -b qtranslate-0.12.0 2>&1 | tee $S/dpkg-source-sentinel.log
   tar -tf qtranslate_0.12.0.tar.xz > $S/tar-sentinel.txt
   ```

   Success:
   - `grep -E '(^|/)(build|build-[^/]*|out|\.claude)(/|$)' $S/tar-sentinel.txt` prints nothing (four
     sentinels excluded; the regex mirrors the globs and does not match
     `.github/workflows/build.yml`);
   - `grep -cE '(^|/)\.git(/|$)' $S/tar-sentinel.txt` is `0` (there is no `.git` in this copy, so the
     meaningful `.git` proof stays step 3);
   - `grep -qx 'qtranslate-0.12.0/CMakeLists.txt'`, `grep -qx 'qtranslate-0.12.0/debian/control'`
     and `grep -qx 'qtranslate-0.12.0/.github/workflows/build.yml' $S/tar-sentinel.txt` succeed
     (the tree was not tarred empty, and `build` does not swallow `build.yml`).

   Then prove that the clean target does not touch a populated `build/` (the user's real tree):

   ```
   cd $S/qtranslate-0.12.0
   mkdir -p build/debian && echo stale > build/debian/stale.txt
   debian/rules clean
   test ! -e build/debian
   test -f build/sentinel
   test -f build-local/sentinel
   test -f out/sentinel
   test -f .claude/sentinel
   ```

   Success: exit 0, `build/debian` gone, all four sentinels still there (measured on this host with
   the same rules file: only `build/debian` is removed).

8. **Extracted source: `-S` and `-b`.** Extract the `.dsc` that step 6 produced:

   ```
   mkdir -p $B
   cd $B
   dpkg-source -x $H/qtranslate_0.12.0.dsc      # warns "unsigned source package"; expected
   cd $B/qtranslate-0.12.0
   test ! -e .git
   test -f debian/source/format
   dpkg-source --print-format .                 # 3.0 (native)
   dpkg-parsechangelog -S Version               # 0.12.0
   dpkg-checkbuilddeps                          # silent
   dpkg-buildpackage -S -us -uc 2>&1 | tee $B/source-only.log
   ```

   Success: `$B/qtranslate_0.12.0.dsc` and `$B/qtranslate_0.12.0.tar.xz` exist; no `.git` in the
   extracted tree (it came from the tarball). Then the binary-only build from the same extracted
   source:

   ```
   DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" \
       dpkg-buildpackage -rfakeroot -us -uc -b 2>&1 | tee $B/binary-only.log
   ls -1 $B | sort
   ```

   No trailing `.`. Success: exit 0; `$B` gains `qtranslate_0.12.0_amd64.deb`,
   `qtranslate-dbgsym_0.12.0_amd64.deb`, `qtranslate_0.12.0_amd64.buildinfo`,
   `qtranslate_0.12.0_amd64.changes`. Do not re-run `packaging/linux/verify-deb.sh` here: the package
   content and `Depends` gate is task 02 (`5c08c21`).

9. **`debian/README.Debian`.** Keep the command block and the `nocheck`/`nodoc`/`parallel`/trusted
   client sections; make two documentation edits:

   - Add the artifacts the build actually writes next to the three already listed:

     ```
       qtranslate-dbgsym_0.12.0_amd64.deb
       qtranslate_0.12.0_amd64.buildinfo
       qtranslate_0.12.0_amd64.changes
     ```

     plus one sentence: the debug symbols are in `qtranslate-dbgsym`, not in the main `.deb`. Move
     the existing sentence "The copyright and changelog are still installed." from the `nocheck`
     paragraph to the `nodoc` paragraph, where it belongs.
   - Add a short "Source package" section: `debian/source/format` is `3.0 (native)`, the tarball is
     built from the current tree (not from a VCS export), `debian/source/options` excludes `build`,
     `build-*`, `out`, `.claude` and `.git`, so a tree copied with `.git` and a local CMake `build/`
     does not leak them into `qtranslate_0.12.0.tar.xz`; `dpkg-source -b <dir>` writes the `.dsc`
     and `.tar.xz` into the current directory and must run on a cleaned tree,
     `dpkg-source -x qtranslate_0.12.0.dsc` extracts `qtranslate-0.12.0/`.

   Success: `grep -n 'dbgsym\|buildinfo\|changes\|tar-ignore\|3.0 (native)' debian/README.Debian`
   shows every item, the command text is unchanged and still has no trailing `.`, and the file stays
   plain ASCII (it is installed as documentation when `nodoc` is not set; the user's `nodoc` build
   does not ship it, so the `.deb` content of step 4/5 does not change).

10. **Final tree check and commit.** In `REPO`:

    ```
    git status --porcelain -uall
    git diff -- debian/
    ```

    Success: only `debian/source/options`, `debian/README.Debian`, `debian/.gitignore` (only if step
    4 added a pattern), `debian/rules` (only if a clean check failed), and this `plan.md` are
    modified; no untracked file, no `src/`, `tests/`, `CMakeLists.txt`,
    `packaging/linux/package.sh`, `packaging/linux/verify-deb.sh`, CI, `debian/control`,
    `debian/changelog` or `debian/copyright` change; the version stays `0.12.0`. Fill `## Outcome`
    with the measured evidence (the `--tar-ignore` line, the residue inventory, the build and clean
    commands with their exit status, the `dpkg-source -x` result, the `-S`/`-b` artifact names, and
    the `lintian` observation about `build-essential`). The coordinator commits the changed `debian/`
    files and this `plan.md` (suggested message: `Harden the native source package and its build
    hygiene`); the lead commits the map root, stage, and journal. No push.

## Done when

- `debian/source/options` keeps the default `-I` list (a bare `tar-ignore` line) plus `tar-ignore`
  for `build`, `build-*`, `out` and `.claude`; a `dpkg-source -b` run prints
  `--tar-ignore --tar-ignore=build --tar-ignore=build-* --tar-ignore=out --tar-ignore=.claude`, and
  a `tar -tf` of the resulting `qtranslate_0.12.0.tar.xz` lists no `.git/` entry even though the
  copied tree contains a real `.git` (19 MB) — while `CMakeLists.txt`, `debian/control`,
  `debian/source/options`, `src/ai/openaichat.cpp` and `packaging/linux/verify-deb.sh` are present.
  The restored default `-I` list contains `.gitignore` and `.gitattributes`, so both `.gitignore`
  files and the root `.gitattributes` are absent from the tarball; that is expected.
- In the sentinel copy (`build/sentinel`, `build-local/sentinel`, `out/sentinel`,
  `.claude/sentinel`) the same `tar -tf` lists none of the four names and still contains
  `.github/workflows/build.yml`.
- `dpkg-source --print-format .` prints `3.0 (native)` in the repository and in the extracted
  source; `dpkg-parsechangelog -S Version` prints `0.12.0` with no Debian revision and equals the
  CMake `project(qtranslate VERSION 0.12.0 …)`; `debian/copyright` has the DEP-5 `Format` URL,
  `Files: *`, `Copyright:`, `License: All-Rights-Reserved` and the "All rights reserved" text, and
  `lintian qtranslate_0.12.0.dsc` reports no copyright or license tag.
- From `$H/qtranslate-0.12.0`, `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc"
  dpkg-buildpackage -rfakeroot -us -uc` (no trailing `.`) exits 0 and writes the `.deb`, `dbgsym`,
  `.dsc`, `.tar.xz`, `.buildinfo` and `.changes` to `$H`; the second build of the same copy, with
  `-tc`, also exits 0. After the first build `git status --porcelain -uall` has no `??` line, and
  `git status --ignored --porcelain -uall` has `!!` lines only for `build/…`, `debian/.debhelper/…`,
  `debian/debhelper-build-stamp`, `debian/files`, `debian/qtranslate/…`,
  `debian/qtranslate.debhelper.log` and `debian/qtranslate.substvars`; after the `-tc` build it has
  no `!!` line.
- `debian/rules clean` on the dirtied copy exits 0 and removes `build/debian`, `debian/files`,
  `debian/qtranslate/`, `debian/.debhelper/`, `debian/debhelper-build-stamp` and all
  `debian/*.substvars`/`debian/*.debhelper*`; an empty `build/` may disappear with `build/debian`,
  while a populated `build/` keeps its other content (sentinel check in step 7); after the clean
  `git status --ignored --porcelain -uall` has no `!!` line, and a later `dpkg-source -b` produces
  the same tarball listing as before the build (`diff` of the two `tar -tf` outputs is empty).
- `dpkg-source -x qtranslate_0.12.0.dsc` extracts `qtranslate-0.12.0/` with no `.git`; from it
  `dpkg-buildpackage -S -us -uc` writes the `.dsc` and `.tar.xz`, and
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc -b`
  exits 0 and writes the `.deb`, `dbgsym`, `.buildinfo` and `.changes`.
- `debian/README.Debian` lists the `.deb`, `dbgsym`, `.buildinfo`, `.changes`, `.dsc` and `.tar.xz`,
  keeps the exact command without the trailing `.`, and documents the native format and the
  `tar-ignore` exclusions including `.git`.
- In `REPO`, `git status --porcelain -uall` shows only `debian/source/options`,
  `debian/README.Debian`, `debian/.gitignore` (only if a pattern was added), `debian/rules` (only if
  a clean fix was required) and this `plan.md`, plus the lead's uncommitted
  `doc/roadmap/debian/journal.md`; `src/`, `tests/`, `CMakeLists.txt`,
  `packaging/linux/package.sh`, `packaging/linux/verify-deb.sh`, CI, `debian/control`,
  `debian/changelog` and `debian/copyright` are unchanged and the version stays `0.12.0`. The
  coordinator commits the task's `debian/` files and this plan; the lead commits the map; no push.

## Risks

- **`.git` in the native tarball is real once the options name a pattern.** Measured with
  `dpkg-source` 1.21.22: a `git init` tree with only `tar-ignore = build` and friends tars
  `.git/…`, because a valued `tar-ignore` replaces the default `-I` list. The fix is the bare
  `tar-ignore` line, which restores the defaults (including `.git`) and keeps the custom patterns;
  `tar-ignore = .git` alone would exclude `.git` but drop `*.o`, `*.so`, `*.a`, `.svn` and the rest.
  The default `.git` pattern matches a `.git` directory and a `.git` file (git worktree) alike.
- **`tar-ignore` is not anchored.** `build` excluded `src/build/sentinel` in a probe, and `build-*`
  matches any base name starting with `build-`. No tracked path is named `build`, `build-*`, `out`
  or `.claude` today (`git ls-files` checked), but a future source directory with one of those names
  would silently disappear from the source package. Keep this in mind before adding such a
  directory.
- **Do not delete `build/`.** `debian/rules:33-35` removes only `build/debian`; in the repository
  `build/` is a 4.1 GB development build tree. A "cleaner" `rm -rf build` would destroy it. Measured
  with the same rules file: a clean kept `build/sentinel` and `build/linux-tools/tool` and removed
  only `build/debian`; only an empty `build/` disappears.
- **A direct `dpkg-source -b` on a dirty tree tars debhelper residue**
  (`debian/qtranslate/usr/bin/qtranslate`, `debian/*.substvars`, `debian/debhelper-build-stamp`, …):
  `tar-ignore` does not cover those paths. `debian/files` and `debian/files.new` are the exception —
  `dpkg-source` always excludes them itself. Always clean first, as `dpkg-buildpackage` does; the
  README says so.
- **`dpkg-source -x` prints "unpacking unsigned source package".** Expected: no signing key is
  configured and the root `## Done when` does not require a signature; do not add one here.
- **`-tc` runs `debian/rules clean` with the gain-root command.** If `clean` fails, the build fails;
  do not add `--no-post-clean` to make it pass. `build/debian` disappearing is not a leftover.
- **`debian/copyright` is not edited.** Any change to the license wording is a user decision
  recorded in the map's risks, not part of this task.
- **`debian/control` is out of scope.** `lintian`'s `E: build-depends-on-build-essential` stays and
  is recorded for stage 2/3 rather than "fixed" here.
- **No push.** The coordinator commits the changed `debian/` files and this `plan.md`; the lead
  commits the map root, stage, and journal.

## Outcome

`debian/source/options` restores `dpkg-source`'s default exclusions and keeps the project patterns;
a native source tarball built from a copy that carries `.git` no longer ships the VCS metadata.

- Changed files: `debian/source/options` (one line) and `debian/README.Debian` (documentation only).
  `debian/.gitignore` is unchanged because the measured residue is already covered, and
  `debian/rules` is unchanged because the clean target had no defect. `src/`, `tests/`,
  `CMakeLists.txt`, `packaging/linux/package.sh`, `packaging/linux/verify-deb.sh`, the CI files,
  `debian/control`, `debian/changelog` and `debian/copyright` are untouched; the version stays
  `0.12.0`.
- `debian/source/options` is five lines, LF, no comments: `tar-ignore`, `tar-ignore = build`,
  `tar-ignore = build-*`, `tar-ignore = out`, `tar-ignore = .claude`. A valued `tar-ignore` replaces
  the default `-I` list; the bare line re-adds it. The four old lines alone shipped the whole `.git`
  (2627 entries, measured while planning). With the five lines `dpkg-source -b` prints
  `--tar-ignore --tar-ignore=build --tar-ignore=build-* --tar-ignore=out --tar-ignore=.claude`.
- Tarball from `/tmp/opencode/qtranslate-hygiene/qtranslate-0.12.0` (copy of the tree with `.git`
  (19 MB) and without `build/`): 573 entries, the single top directory `qtranslate-0.12.0/`, zero
  `.git/` entries and zero `build/` entries. `qtranslate-0.12.0/CMakeLists.txt`,
  `debian/control`, `debian/source/options`, `src/ai/openaichat.cpp` and
  `packaging/linux/verify-deb.sh` are present.
- The tarball holds the tracked tree minus three files: 463 `git ls-files` paths against 460 file
  entries in `tar -tf`, and `comm` shows exactly `.gitattributes`, `.gitignore` and
  `debian/.gitignore` missing, with no extra path.
- Sentinel copy (`build/sentinel`, `build-local/sentinel`, `out/sentinel`, `.claude/sentinel`):
  `tar -tf` lists none of the four, and `.github/workflows/build.yml`, `CMakeLists.txt` and
  `debian/control` are still there.
- Build in the copy, `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc"
  dpkg-buildpackage -rfakeroot -us -uc` (no trailing `.`): exit 0; the parent directory gains
  `qtranslate_0.12.0_amd64.deb`, `qtranslate-dbgsym_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc`,
  `qtranslate_0.12.0.tar.xz`, `qtranslate_0.12.0_amd64.buildinfo` and
  `qtranslate_0.12.0_amd64.changes`.
- Residue inventory after that build, collapsed: `build/...`, `debian/.debhelper/...`,
  `debian/debhelper-build-stamp`, `debian/files`, `debian/qtranslate/...`,
  `debian/qtranslate.debhelper.log`, `debian/qtranslate.substvars`; `git status --porcelain -uall`
  has no `??` line, and `debian/qtranslate-dbgsym/` is not created (the debug root is
  `debian/.debhelper/qtranslate/dbgsym-root`).
- `debian/rules clean` on that tree exits 0 and removes `build/debian`, `debian/files`,
  `debian/qtranslate/`, `debian/.debhelper/`, `debian/debhelper-build-stamp` and the
  `debian/*.substvars`/`*.debhelper*` files; the empty `build/` disappears with `build/debian`;
  `git status --ignored --porcelain -uall` then has no `!!` line, `git status -uall` has no `??`
  line, and a second `debian/rules clean` also exits 0.
- In the sentinel copy with a populated `build/` (the four sentinels plus a stale
  `build/debian/stale.txt`), `debian/rules clean` removes only `build/debian` and keeps all four
  sentinels.
- The second build of the same copy with `-tc` exits 0 (pre-clean and post-clean are in the log),
  writes the same six artifacts and leaves the tree clean again. A later `dpkg-source -b` produces
  a `tar -tf` listing identical to the pre-build one (`diff` empty; also re-checked after the final
  `README.Debian` edit).
- `dpkg-source -x qtranslate_0.12.0.dsc` extracts `qtranslate-0.12.0/` with no `.git`;
  `dpkg-source --print-format .` is `3.0 (native)` and `dpkg-parsechangelog -S Version` is `0.12.0`
  there. `dpkg-buildpackage -S -us -uc` exits 0 and writes the `.dsc` and `.tar.xz` (plus
  `qtranslate_0.12.0_source.buildinfo` and `qtranslate_0.12.0_source.changes`);
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc -b`
  exits 0 and writes the `.deb`, the dbgsym `.deb`, the `.buildinfo` and the `.changes`. The
  `dpkg-source -x` "unsigned source package" warning is expected.
- `debian/README.Debian` lists the six artifacts, says the debug symbols are in
  `qtranslate-dbgsym`, and documents the native format, the five `tar-ignore` lines (the bare one
  restores the defaults), the "clean before `dpkg-source -b`" rule and the `dpkg-source -x` result.
  The command block is unchanged and has no trailing `.`, the file is plain ASCII, and `nodoc` still
  keeps it out of the `.deb`.
- `lintian qtranslate_0.12.0.dsc` reports no copyright or license tag; its only tag is
  `E: qtranslate source: build-depends-on-build-essential`. `debian/control` is out of scope and the
  tag is left for stage 2/3.

What the work found (the plan corrections are applied above):

- **The `.gitignore` expectation was a measurement artifact.** The first revision of step 3 and
  `## Done when` expected both `.gitignore` files to stay in the tarball; that held only while
  valued `tar-ignore` lines had replaced the default list. The restored default `-I` list contains `.gitignore` and
  `.gitattributes` (`/usr/share/perl5/Dpkg/Source/Package.pm`), so the bare line necessarily drops
  them. They stay in git, are not needed for a build, and the extracted `-b` build passes without
  them. No pattern was added to bring them back: a valued line would drop `.git` and the rest of the
  defaults again.
- `dpkg-buildpackage -S` also writes `qtranslate_0.12.0_source.buildinfo` and
  `qtranslate_0.12.0_source.changes` next to the `.dsc`/`.tar.xz`; the plan named only the latter
  two.
- `.git` measures 19 MB in the rsync copy (`du -sh`) and 20 MB in the repository; the size has no
  impact, and `## Done when` uses the copy's 19 MB.
- **`debian/files` is always excluded by `dpkg-source`.** A dirty-tree probe (a fresh copy with
  `debian/files`, `debian/qtranslate/usr/bin/qtranslate`, `debian/qtranslate.substvars` and
  `debian/debhelper-build-stamp`) left `debian/files` out of the tarball while the other three went
  in: `Dpkg::Source::Package::init_options` pushes `debian/files` and `debian/files.new` into
  `tar_ignore` regardless of `debian/source/options`. Step 6, the risk note and
  `debian/README.Debian` now name only the paths that do leak.
- `git status --ignored --porcelain -uall` still prints the modified tracked files (` M`), so it is
  "no `!!` line", not "empty"; the step 5 and `## Done when` checks now say that.

Not done:

- The Debian build's test suite (without `nocheck`) is not re-run: `nocheck` is the user's command
  and task 01 measured 45/45. The acceptance run is the project's own Debug and Release CTest
  suites on the final tree (45/45 each, no compiler warnings), and this task changes neither the
  code nor `debian/rules`.
- The user's 4.1 GB `build/` in the repository was not touched; the Debian builds ran in
  `/tmp/opencode` copies.
- The `lintian` `build-depends-on-build-essential` tag stays for stage 2/3, as the plan says.

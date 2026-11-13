---
kind: task
status: done
detailed: true
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

## Steps

Facts measured on this machine before planning (2026-11-14, `debian:trixie` 13.7 with the apt
lists updated): `debhelper` 13.24.2 `Provides: debhelper-compat (= 9, 10, 11, 12, 13)`, so
`debian/control:8`'s `debhelper-compat (= 13)` is satisfied; `pkg-config` 1.8.1-4 and `fakeroot`
1.37.1.1-1 are trixie packages; `apt-get -s build-dep .` in trixie exits 0 without `deb-src`
entries, so the gate's `apt-build-dep` check stays a pass and the summary stays 15 checks;
`curl -sIL` to the pinned appimagetool and linuxdeploy URLs returns 200, so the AppImage tool
download works in the container. `dpkg-checkbuilddeps` prints `warning: cannot determine CC
system type` when no compiler is installed (a probe container with only `dpkg-dev`); with the
package list installed `build-essential` is present and the gate's `build-deps` check, which
requires empty output, is silent. `qt6-base-dev` depends on `qmake6`, so `/usr/bin/qmake6` exists
for the AppImage step (`package.sh:864`), and the AppImage tools run with
`APPIMAGE_EXTRACT_AND_RUN=1` (`:898`), so no FUSE is needed.

1. **Add the three native-build dependencies to `packaging/linux/trixie-packages.txt`.** Keep the
   one-package-per-line layout and the section comments. The "Build toolchain" section gains one
   line after `ninja-build`:

   ```
   # Build toolchain.
   build-essential
   cmake
   ninja-build
   pkg-config
   ```

   The "Packaging gates and tools" section (the two comment lines and
   `desktop-file-utils`/`dpkg-dev`/`patchelf`) becomes:

   ```
   # Packaging: dpkg-buildpackage and the debhelper dh sequence build the native
   # .deb, fakeroot is the documented -rfakeroot command, desktop-file-validate
   # and dpkg-shlibdeps are the verify-deb.sh gate, patchelf is the bundle RPATH.
   debhelper
   fakeroot
   desktop-file-utils
   dpkg-dev
   patchelf
   ```

   `dpkg-dev` is already listed (`:38`). `pkg-config` is in `Build-Depends`
   (`debian/control:10`) and is what the implicit `dpkg-checkbuilddeps` inside `dpkg-buildpackage`
   was missing; it also serves CMake's `PkgConfig` lookup for the optional libsecret. `fakeroot`
   is not used by the script's `dpkg-buildpackage -b -us -uc` (`Rules-Requires-Root: no`,
   `debian/control:7`), but the user's documented `dpkg-buildpackage -rfakeroot -us -uc` command
   needs it, which is why the list carries it. `packaging/linux/trixie.Dockerfile:17-22` reads the
   same file and needs no change; the CI install step filters comments with
   `grep -vE '^[[:space:]]*(#|$)'` and keeps the new lines. Success:
   `grep -nE '^(debhelper|fakeroot|pkg-config)$' packaging/linux/trixie-packages.txt` prints three
   lines, and `git diff` of the file touches only those lines and the two comment lines.

2. **Add the `verify-deb.sh` step to the Linux job.** In `.github/workflows/build.yml`, between
   `Package` (`:116-117`) and `Upload artifacts` (`:119-125`), insert:

   ```yaml
         - name: Verify the native package
           run: ./packaging/linux/verify-deb.sh build/artifacts-linux/qtranslate_*_amd64.deb
   ```

   The glob `qtranslate_*_amd64.deb` cannot match `qtranslate-dbgsym_0.12.0_amd64.deb` (the
   literal prefix is `qtranslate_`), and the dbgsym is not in `build/artifacts-linux` at all:
   `package.sh` keeps it in `build/artifacts-linux/.qtranslate-staging`. If no `.deb` was written,
   the unexpanded glob reaches the script and it exits 2 with `package ... does not exist`, so the
   step fails. Do not pass `--source-dir`: the script defaults to `realpath
   "$script_dir/../.."` (`packaging/linux/verify-deb.sh:243-244`), which is the checkout with
   `debian/control`, `debian/changelog` and `CMakeLists.txt`; the `deb-src` copy inside the
   staging directory is removed by `package.sh` (`:853`), so the checkout is the only complete
   source tree. Do not run the gate through `sudo`: the container job runs as root
   (`verify-deb.sh:290-291`), so `install-roundtrip` uses `dpkg` directly. Keep the step order
   Package → Verify → Upload.

3. **Keep the hidden staging directory out of the upload.** In the same job's `Upload artifacts`
   step add `include-hidden-files: false` after `if-no-files-found: error`:

   ```yaml
         - name: Upload artifacts
           uses: actions/upload-artifact@v7
           with:
             name: qtranslate-linux-x64
             path: build/artifacts-linux/*
             if-no-files-found: error
             include-hidden-files: false
             retention-days: 14
   ```

   The top level of the output directory is the four release files; the only hidden entry is
   `.qtranslate-staging/`, which holds the `qtranslate-dbgsym_0.12.0_amd64.deb`, the `.changes` and
   the `.buildinfo`. `false` is the action default since `upload-artifact` v4.4, but the user's "do
   not publish dbgsym" decision depends on it, so the workflow states it. The name, the path and
   `if-no-files-found` stay as they are.

4. **Do not change the release job; record its invariants.** Read
   `.github/workflows/build.yml:127-178` and state in the report that all of the following stay
   unchanged: `if: startsWith(github.ref, 'refs/tags/v')` (`:129`), `needs: [windows, linux]`
   (`:130`), `permissions: contents: write` (`:132-133`), the two downloads
   `qtranslate-windows-x64` and `qtranslate-linux-x64` into `artifacts` (`:139-149`), and the
   `gh release create|upload "$tag" artifacts/* ...` block (`:151-177`). The native `.deb` and the
   gate change what the Linux artifact contains, not how the release job consumes it: the artifact
   name and the `artifacts/*` glob are unchanged, and the AppImage, the tar.gz and
   `SHA256SUMS-linux.txt` keep their names. The release job does not push or tag; its `gh release`
   step is the pre-existing, tag-gated release publication, and it does not run here.

5. **Parse the YAML and check the job shape on the host** (this host has `ruby` with YAML and no
   `python3-yaml`):

   ```bash
   ruby -ryaml -e 'wf = YAML.load_file(".github/workflows/build.yml"); p wf["jobs"].keys; p wf["jobs"]["linux"]["steps"].map { |s| s["name"] }'
   ```

   Expect `["windows", "linux", "release"]` and the Linux step names ending with
   `["Package", "Verify the native package", "Upload artifacts"]`. Then the static invariants:

   ```bash
   grep -n 'container: debian:trixie' .github/workflows/build.yml
   grep -n 'timeout 300 xvfb-run -a ctest' .github/workflows/build.yml
   grep -n 'name: qtranslate-linux-x64' .github/workflows/build.yml
   grep -n 'path: build/artifacts-linux/\*' .github/workflows/build.yml
   grep -n 'include-hidden-files: false' .github/workflows/build.yml
   grep -n 'startsWith(github.ref' .github/workflows/build.yml
   grep -n 'needs: \[windows, linux\]' .github/workflows/build.yml
   grep -n 'name: qtranslate-windows-x64' .github/workflows/build.yml
   bash -n packaging/linux/package.sh
   bash -n packaging/linux/verify-deb.sh
   git diff --stat
   ```

   Each `grep` finds one line; the two `bash -n` runs are silent (`package.sh` and `verify-deb.sh`
   are not edited); `git diff --stat` lists only `packaging/linux/trixie-packages.txt`,
   `.github/workflows/build.yml` and this `plan.md`.

6. **Make a fresh copy of the tree outside the repository.** `package.sh` and `debian/rules` write
   `build/ci`, `build/artifacts-linux`, `build/debian` and the hidden staging, so the working tree
   must not be the build directory. Use a tar copy of the working tree — `git archive HEAD` would
   test the old, uncommitted list — with the same exclusions the source package and `package.sh`
   use:

   ```bash
   rm -rf /tmp/opencode/ci-trixie && mkdir -p /tmp/opencode/ci-trixie/src
   cd /home/parsee/sync/mine/QTranslate
   tar -C "$PWD" -cf - --exclude=./.git --exclude=./build --exclude='./build-*' \
       --exclude=./out --exclude=./.claude . | tar -C /tmp/opencode/ci-trixie/src -xf -
   test ! -e /tmp/opencode/ci-trixie/src/.git
   test ! -e /tmp/opencode/ci-trixie/src/build
   grep -nE '^(debhelper|fakeroot|pkg-config)$' /tmp/opencode/ci-trixie/src/packaging/linux/trixie-packages.txt
   test -x /tmp/opencode/ci-trixie/src/debian/rules
   ```

   `build` must not be copied: this working tree's `build/artifacts-linux` holds 0.11.0 leftovers
   (`qtranslate_0.11.0_amd64.deb`, `qtranslate_0.11.0+astra1_amd64.deb`); a stale
   `qtranslate_0.11.0_amd64.deb` would make the verify glob match two packages and the upload ship
   stale files. The copy plus the two builds and the artifacts need ~3-4 GiB; `/tmp` is a 32 GiB
   tmpfs (11 GiB free at planning time) and `/home` has 156 GiB free — if the tmpfs runs out, move
   the copy to `/home/parsee/.cache/ci-trixie` and keep the commands otherwise unchanged.

7. **Generate the job script from the workflow and run it in `debian:trixie`.** Extracting the
   `run:` blocks keeps the local commands identical to the file; the `Check out` step has no
   `run:` and is skipped because the copy already contains the tree.

   ```bash
   ruby -ryaml -e '
     wf = YAML.load_file(".github/workflows/build.yml")
     puts "set -e"
     wf["jobs"]["linux"]["steps"].each do |step|
       next unless step["run"]
       puts "echo " + "== #{step["name"]}".inspect
       puts step["run"]
       puts "set -e"
     end
   ' > /tmp/opencode/ci-trixie/linux-steps.sh
   cat /tmp/opencode/ci-trixie/linux-steps.sh
   ```

   The script runs `apt-get update && apt-get install -y --no-install-recommends git
   ca-certificates`, `apt-get update` plus the `grep -vE ... | xargs apt-get install` list, CMake
   configure with `-DCMAKE_PREFIX_PATH=/usr`, `cmake --build build/ci --parallel`,
   `timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure`, `package.sh` and
   `verify-deb.sh`. The repeated `set -e` mirrors the action's per-step `bash -e`. The AppImage
   tools are downloaded into `build/linux-tools/appimage`, which is not in the copy.

   The `dpkg-checkbuilddeps` criterion is not a workflow step, so it goes in a second file and runs
   in the same container after the steps:

   ```bash
   cat > /tmp/opencode/ci-trixie/extra-checks.sh <<'EOF'
   set -e
   cd /src
   echo "== dpkg-checkbuilddeps (silent expected)"
   out=$(dpkg-checkbuilddeps 2>&1)
   printf '%s' "$out"
   [[ -z $out ]]
   EOF
   ```

   Run it with a generous timeout: apt, two Release builds (CMake and `dpkg-buildpackage`), the 45
   tests and the AppImage take roughly 10-20 minutes, and the shell tool's 120 s default is too
   short — use `background: true` or `timeout: 3600000`:

   ```bash
   timeout 3600 docker run --rm \
     -v /tmp/opencode/ci-trixie/src:/src \
     -v /tmp/opencode/ci-trixie:/work \
     -e DEBIAN_FRONTEND=noninteractive \
     -w /src debian:trixie \
     bash -c 'bash /work/linux-steps.sh && bash /work/extra-checks.sh' \
     > /tmp/opencode/ci-trixie/run.log 2>&1
   echo "exit: $?"
   ```

   Do not interrupt the run: the container is `--rm`, so a re-run starts clean, and the copy only
   needs to be recreated if the tree changed. `docker` writes as root into
   `/tmp/opencode/ci-trixie/src`; do not use `--user`, because `verify-deb.sh` needs root for its
   install roundtrip.

8. **Check the log.** `grep` on `/tmp/opencode/ci-trixie/run.log` for the expected lines:

   ```bash
   grep -nE 'tests passed|== summary:|^Package summary:|^  (artifact|deb|deb size|depends|installed size|appimage):' \
       /tmp/opencode/ci-trixie/run.log
   ```

   Expect `100% tests passed, 0 tests failed out of 45`; `== summary: 15 checks, 15 passed, 0
   failed, 0 skipped`; the `Package summary:` block with
   `artifact: build/artifacts-linux/qtranslate-0.12.0-linux-x86_64.tar.gz`,
   `deb: build/artifacts-linux/qtranslate_0.12.0_amd64.deb`,
   `appimage: build/artifacts-linux/qtranslate-0.12.0-x86_64.AppImage`, a non-empty `depends:` and
   a non-empty `installed size:`. The `dpkg-checkbuilddeps` section prints nothing after its
   header (`extra-checks.sh` exits non-zero on any output). The gate prints no `FAIL`, `SKIP` or
   `WARN` lines: `apt-build-dep` is a `PASS` with a note, `build-deps` is a `PASS`, and
   `install-roundtrip` is a `PASS`.

9. **Check the artifact set and the checksums** from the copy (the container wrote them as root;
   the files are readable):

   ```bash
   ls -A /tmp/opencode/ci-trixie/src/build/artifacts-linux
   cat /tmp/opencode/ci-trixie/src/build/artifacts-linux/SHA256SUMS-linux.txt
   ( cd /tmp/opencode/ci-trixie/src/build/artifacts-linux && sha256sum -c SHA256SUMS-linux.txt )
   ls -A /tmp/opencode/ci-trixie/src/build/artifacts-linux/.qtranslate-staging
   dpkg-deb -f /tmp/opencode/ci-trixie/src/build/artifacts-linux/qtranslate_0.12.0_amd64.deb \
       Package Version Architecture Depends
   ```

   Expect the top level to be exactly `.qtranslate-staging`,
   `qtranslate-0.12.0-linux-x86_64.tar.gz`, `qtranslate-0.12.0-x86_64.AppImage`,
   `qtranslate_0.12.0_amd64.deb` and `SHA256SUMS-linux.txt`; `SHA256SUMS-linux.txt` to list the
   three files (`sha256sum` prints three `OK` lines); the staging to hold
   `qtranslate_0.12.0_amd64.deb`, `qtranslate-dbgsym_0.12.0_amd64.deb`,
   `qtranslate_0.12.0_amd64.changes`, `qtranslate_0.12.0_amd64.buildinfo`, `install/`, the bundle
   directory and `AppDir/`, and no dbgsym, `.changes` or `.buildinfo` at the top level;
   `dpkg-deb -f` to give `Package: qtranslate`, `Version: 0.12.0`, `Architecture: amd64` and a
   `Depends` that the gate's `depends-shlibs` check already accepted as the host's
   `dpkg-shlibdeps` output (the trixie package depends on `libglib2.0-0t64` and
   `libxtst6 (>= 2:1.2.5)`, as the stage-1 container run recorded).

10. **Clean up and hand off.** `git status --short` in the repository shows only
    `packaging/linux/trixie-packages.txt`, `.github/workflows/build.yml` and this `plan.md` (plus
    the map files the lead owns); the working tree has no `build/ci` or `build/artifacts-linux`
    changes from this run. The copy under `/tmp/opencode/ci-trixie` is root-owned; leave it as
    evidence or remove it with `sudo rm -rf /tmp/opencode/ci-trixie`. No commit and no push: the
    coordinator commits the two files and this plan, the lead commits the map.

Risks and easy mistakes:

- **The gate's `install-roundtrip` needs root.** It only installs and removes when `id -u` is 0 or
  `sudo -n` works (`verify-deb.sh:290-291`, `:807-812`); a non-root container would skip it and
  the summary would be `15 checks, 14 passed, 0 failed, 1 skipped`, not `15 passed`. Do not use
  `docker run --user`.
- **The gate's `build-deps` check requires silence.** `dpkg-checkbuilddeps` prints `cannot
  determine CC system type` when no compiler is installed; that only happens without
  `build-essential`, which is in the list. Do not loosen the check: the package list is the fix.
- **`apt-build-dep` must not warn.** With the trixie lists `apt-get -s build-dep .` exits 0 and the
  check is a pass; a summary of `14 checks` means the run is not comparable to the stage-1 record
  and must be investigated before the gate is declared passed.
- **Do not add `--source-dir` to the workflow.** The `deb-src` copy is deleted; the checkout is the
  source tree, and a wrong `--source-dir` would make `dpkg-parsechangelog`, `dpkg-shlibdeps` or
  `dpkg-checkbuilddeps` read the wrong files.
- **Timing and disk.** `timeout 300` is the CI's own guard; keep it, and if the local run exceeds
  it, record the elapsed time instead of raising the value. `/tmp` is a tmpfs here: the copy and
  the two builds need ~3-4 GiB.
- **The AppImage download.** The pinned tools are fetched on every run and SHA256-verified; a
  changed upstream asset fails the run by design (`package.sh:431-440`). The pinned URLs answered
  200 at planning time.

## Done when

- A local `debian:trixie` run of the job's steps on a fresh copy of the tree exits 0, in this
  order: `apt-get install` the package list (including the three new entries), CMake configure and
  build in `build/ci`, `timeout 300 xvfb-run -a ctest --test-dir build/ci` with 45/45
  (`100% tests passed, 0 tests failed out of 45`),
  `./packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux`, and
  `./packaging/linux/verify-deb.sh build/artifacts-linux/qtranslate_0.12.0_amd64.deb` ending with
  `== summary: 15 checks, 15 passed, 0 failed, 0 skipped`; the commands come from the `run:` blocks
  of `jobs.linux.steps` (step 7) and the copy is outside the repository.
- The top level of `build/artifacts-linux` holds the four release files
  `qtranslate-0.12.0-linux-x86_64.tar.gz`, `qtranslate_0.12.0_amd64.deb`,
  `qtranslate-0.12.0-x86_64.AppImage` and `SHA256SUMS-linux.txt`, plus the hidden
  `.qtranslate-staging/` directory and nothing else; `SHA256SUMS-linux.txt` lists exactly those
  three files; `sha256sum -c SHA256SUMS-linux.txt` passes; `.qtranslate-staging/` holds the
  `dbgsym`, the `.changes` and the `.buildinfo`, and none of them is at the top level or in the
  checksums.
- `dpkg-checkbuilddeps` inside the same container run is silent (exit 0, no output); this is also
  the check that `pkg-config` was missing, and the gate's `build-deps` check reports the same.
- `.github/workflows/build.yml` parses as YAML (`ruby -ryaml -e 'YAML.load_file(".github/workflows/build.yml")'`
  on this host, which has no `python3-yaml`), and the job invariants hold: the Linux job has the
  `Verify the native package` step after `Package` and keeps `container: debian:trixie`, the
  `timeout 300 xvfb-run -a ctest` step, the same `upload-artifact` name `qtranslate-linux-x64`,
  path `build/artifacts-linux/*` and `if-no-files-found: error`, plus
  `include-hidden-files: false`; the release job keeps `if: startsWith(github.ref,
  'refs/tags/v')`, `needs: [windows, linux]`, `permissions: contents: write`, both downloads and
  the `gh release` step; no step is added or changed that pushes or tags, and the release job is
  not edited.
- No GitHub workflow run, push, tag or release is performed; only the local container run and the
  YAML parse.
- Besides this plan, only `packaging/linux/trixie-packages.txt` and
  `.github/workflows/build.yml` are changed.

## Outcome

The Linux CI job installs the native build dependencies, gates the native `.deb` with the stage-1
`verify-deb.sh` and uploads the same `qtranslate-linux-x64` set; the release job is unchanged.

- Changed files: `packaging/linux/trixie-packages.txt` (+5/−2) and `.github/workflows/build.yml`
  (+4). `packaging/linux/package.sh`, `packaging/linux/verify-deb.sh`, `debian/`, `trixie.Dockerfile`,
  `src/`, `tests/`, `CMakeLists.txt`, `README*` and the other docs are untouched.
- `trixie-packages.txt`: `pkg-config` joins "Build toolchain" after `ninja-build`; `debhelper` and
  `fakeroot` join "Packaging gates and tools" before `desktop-file-utils`, and the two-line comment
  becomes three lines naming `dpkg-buildpackage`/`dh`, `-rfakeroot` and the `verify-deb.sh` tools.
  `dpkg-dev` stays single. The CI install step's `grep -vE '^[[:space:]]*(#|$)'` filter drops the
  comment lines, and `trixie.Dockerfile` reads the same list and needed no change.
- `build.yml`, Linux job: a new `Verify the native package` step between `Package` and
  `Upload artifacts` runs `./packaging/linux/verify-deb.sh
  build/artifacts-linux/qtranslate_*_amd64.deb` without `--source-dir` (the script's default source
  tree is the checkout with `debian/` and `CMakeLists.txt`) and without `sudo` (the container runs as
  root, so `install-roundtrip` really installs and removes). `Upload artifacts` states
  `include-hidden-files: false` next to the unchanged `name`, `path` and `if-no-files-found: error`,
  so the hidden `.qtranslate-staging` cannot be published even if the action default changes.
- Local `debian:trixie` run (this machine, trixie 13.7): a tar copy of the working tree (minus
  `.git`, `build`, `build-*`, `out`, `.claude`) under `/tmp/opencode/ci-trixie/src`, the commands
  taken verbatim from the `run:` blocks of `jobs.linux.steps`, `docker run` as root without `--user`.
  Exit 0. The list installed `debhelper` 13.24.2, `fakeroot` 1.37.1.1-1 and `pkg-config` 1.8.1-4 as
  new packages; configure and build ran in `build/ci`; `timeout 300 xvfb-run -a ctest` reported
  `100% tests passed, 0 tests failed out of 45` in 23.8 s; `package.sh` built the `.deb` with
  `dpkg-buildpackage` in the staging copy; `verify-deb.sh` on
  `build/artifacts-linux/qtranslate_0.12.0_amd64.deb` ended with
  `15 checks, 15 passed, 0 failed, 0 skipped` and printed no `FAIL`, `SKIP` or `WARN` (two `NOTE`
  lines from `apt-build-dep` record the dry run against the installed packages, 0 newly installed);
  the following `dpkg-checkbuilddeps` in `/src` printed nothing. The whole job took about 3 minutes on
  24 cores, against the 10-20 minutes estimated here; `timeout 300` stays as it is and the test step
  needed 24 s.
- Artifact set: the top level holds exactly `qtranslate-0.12.0-linux-x86_64.tar.gz`,
  `qtranslate_0.12.0_amd64.deb`, `qtranslate-0.12.0-x86_64.AppImage`, `SHA256SUMS-linux.txt` and the
  hidden `.qtranslate-staging/`; `sha256sum -c SHA256SUMS-linux.txt` prints three `OK` lines and the
  checksum file lists only those three files; the staging holds the `.deb`, the
  `qtranslate-dbgsym_0.12.0_amd64.deb`, the `.changes`, the `.buildinfo`, `install/`, the bundle and
  `AppDir/`, and none of them is an artifact. `dpkg-deb -f` gives `Package: qtranslate`,
  `Version: 0.12.0`, `Architecture: amd64` and a `Depends` from `libc6 (>= 2.34)` through
  `libqt6core6t64 (>= 6.8.2)`, `libsecret-1-0` and `libxtst6 (>= 2:1.2.5)`.
- Release job: read only, not edited. `if: startsWith(github.ref, 'refs/tags/v')`,
  `needs: [windows, linux]`, `runs-on: ubuntu-latest`, `permissions: contents: write`, both
  `actions/download-artifact@v8` downloads (`qtranslate-windows-x64`, `qtranslate-linux-x64` into
  `artifacts`) and the `gh release view|upload|edit|create "$tag" artifacts/*` block are as before;
  no step pushes, tags or acts outside the tag-gated release.
- Plan deviation: step 5 says "each grep finds one line", but `qtranslate-linux-x64`,
  `qtranslate-windows-x64` and `startsWith(github.ref` match twice (upload and download, the Windows
  tag check and the release `if`). That was already so at `0794258` and the second matches sit in
  places the task must not edit, so nothing changed; `container: debian:trixie`,
  `timeout 300 xvfb-run -a ctest`, `path: build/artifacts-linux/*`, `include-hidden-files: false` and
  `needs: [windows, linux]` match once each.
- `.github/workflows/build.yml` parses as YAML on this host (`ruby -ryaml`), and the Linux step names
  end with `Package`, `Verify the native package`, `Upload artifacts`. The glob cannot match
  `qtranslate-dbgsym_*` (its literal prefix is `qtranslate_`); with no `.deb` in the directory the
  unexpanded glob reaches the script and the step fails with `package ... does not exist` (exit 2),
  checked by running the gate against the stale 0.11.0 artifacts before the local run.
- Not exercised here: GitHub Actions itself, the tag-gated release publication and the push (local
  commit only). Review found nothing.

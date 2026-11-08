---
kind: task
status: done
detailed: true
depends: [02-portable-tarball, 03-deb-package, 04-appimage]
---

# Linux CI job and release publication

## Task

Add the Linux job to `.github/workflows/build.yml` and extend the release job so a `v*` tag
publishes both the Windows and the Linux artifacts. The Linux job runs inside a `debian:trixie`
container (the host runner may stay `ubuntu-latest`), builds Release, runs `xvfb-run -a ctest` under
`timeout 300`, runs `packaging/linux/package.sh`, and uploads `qtranslate-linux-x64`. The existing
Windows job and its tag/version check stay unchanged and still gate the release.

State measured before planning (2026-11-08):

- `.github/workflows/build.yml` has only the `windows` job (`actions/checkout@v7`,
  `jurplel/install-qt-action@v4` with Qt 6.8.3, `ilammy/msvc-dev-cmd@v1.9.0`, the tag/version check,
  `actions/upload-artifact@v7`, `qtranslate-windows-x64`) and the `release` job (`needs: windows`,
  one `actions/download-artifact@v8` into `artifacts`, `gh release upload artifacts/* --clobber`).
  There is no `linux` job yet. `on:`, `concurrency:`, and the top-level `permissions: contents:
  read` are outside the jobs; only `release` has `contents: write`.
- Windows artifact names are `qtranslate-<version>-win64.zip`,
  `qtranslate-<version>-win64-setup.exe`, and `SHA256SUMS.txt` (`packaging/windows/package.ps1:151`,
  `:179`, `:190`). The Linux names `qtranslate-<version>-linux-x86_64.tar.gz`,
  `qtranslate_<version>_amd64.deb`, `qtranslate-<version>-x86_64.AppImage`, and
  `SHA256SUMS-linux.txt` do not collide with them, so both sets can share `artifacts/`.
- `packaging/linux/trixie.Dockerfile` keeps the 21-package apt list inline. `package.sh` needs
  `cmake`, `qtpaths6`, `qmake6` (AppImage build only), `patchelf`, `tar`, `sha256sum`,
  `desktop-file-validate`, `readelf`, `ldd`, `dpkg-deb`, `dpkg-shlibdeps`, `du`, `curl` (AppImage
  only), and the usual coreutils/findutils tools. The default target is `trixie` with the AppImage
  enabled; the artifacts go to `--output-dir`, and the AppImage tools are downloaded into
  `build/linux-tools/appimage` with pinned SHA256 values.
- In `qtranslate-trixie:latest` (Qt 6.8.2): `binutils` 2.44-3 is installed through `gcc-14` and
  `dpkg-dev`; `qt6-base-dev-tools`, which provides `qtpaths6` and `qmake6`, is installed through
  `qt6-base-dev`; `coreutils`, `findutils`, `tar`, `grep`, and `sed` are `Essential: yes` in the
  base image; `patch` exists but `package.sh` does not use it; `python3` exists but PyYAML does not.
- `actions/checkout@v7` sets the repository path as a git safe directory for container jobs
  (`setSafeDirectory` in `git-source-provider.ts` of v7.0.1), so running as root in the container
  should not hit "dubious ownership"; the fallback stays in `## Risks`.
- The local host has Docker, `qtranslate-trixie:latest`, `debian:trixie`, the cached AppImage tools,
  and Ruby Psych for a YAML syntax check. The actual GitHub run, a tag push, and a release
  publication need the user's explicit approval and are not performed by this task.

Contract:

- Keep one apt package list for the local image and CI. Create
  `packaging/linux/trixie-packages.txt` (one package per line, `#` comments allowed) containing the
  full set: `git ca-certificates build-essential cmake ninja-build qt6-base-dev qt6-tools-dev
  qt6-tools-dev-tools qt6-translations-l10n libx11-dev libxtst-dev libsecret-1-dev dbus xvfb xauth
  desktop-file-utils dpkg-dev patchelf qt6-wayland file curl`. `packaging/linux/trixie.Dockerfile`
  installs from this file (copy it into the image and use `xargs apt-get install`), and the Linux CI
  job installs from the same file after checkout. Do not duplicate the list in the workflow.
- The Linux job:

  ```yaml
  linux:
    name: Linux x64 (trixie)
    runs-on: ubuntu-latest
    timeout-minutes: 60
    container: debian:trixie
    env:
      DEBIAN_FRONTEND: noninteractive

    steps:
      - name: Install checkout prerequisites
        run: apt-get update && apt-get install -y --no-install-recommends git ca-certificates

      - name: Check out
        uses: actions/checkout@v7

      - name: Install build and packaging dependencies
        run: |
          apt-get update
          grep -vE '^[[:space:]]*(#|$)' packaging/linux/trixie-packages.txt \
            | xargs apt-get install -y --no-install-recommends

      - name: Configure
        run: cmake -S . -B build/ci -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr

      - name: Build
        run: cmake --build build/ci --parallel

      - name: Test
        run: timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure

      - name: Package
        run: ./packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux

      - name: Upload artifacts
        uses: actions/upload-artifact@v7
        with:
          name: qtranslate-linux-x64
          path: build/artifacts-linux/*
          if-no-files-found: error
          retention-days: 14
  ```

  Use `DEBIAN_FRONTEND=noninteractive` for the apt steps (job `env:` or inline). Keep
  `permissions: contents: read` at the top level; only the `release` job has `contents: write`.
  The Linux job runs on push/PR/workflow_dispatch like the Windows job, not only on tags.
- The release job: change `needs: windows` to `needs: [windows, linux]`; add a second
  `actions/download-artifact@v8` step for `qtranslate-linux-x64` with `path: artifacts` (the same
  directory as `qtranslate-windows-x64`); the existing `gh release upload artifacts/* --clobber`
  then publishes both sets. `SHA256SUMS-linux.txt` and the Windows `SHA256SUMS.txt` coexist because
  the names differ. Keep the notes logic and `--generate-notes` fallback unchanged.
- Do not change the Windows job, its `install-qt-action`, its tag/version check, or the `on:` /
  `concurrency:` blocks. The tag/version check in the Windows job is the single gate; the Linux
  job derives the version from `CMakeLists.txt` through `package.sh` and does not duplicate it.
- The `package.sh` AppImage build downloads the pinned tools; the CI network is available. Do not
  pass `--skip-appimage` in CI. If the download or checksum fails, the job fails; the artifact is
  not silently omitted.
- CI cannot build the Astra artifacts (no Astra runner; the native Astra build is child 05). The
  outcome states this; publishing the Astra artifacts, if desired, is a manual, user-approved
  action outside this job.
- The actual GitHub run and any tag publication require the user's explicit push approval. Until
  then, record them as not performed, never as passed.

## Steps

1. Create `packaging/linux/trixie-packages.txt` as the single apt list for the image and CI.
   - One package per line; blank lines and lines starting with `#` are allowed and are stripped by
     the consumers with `grep -vE '^[[:space:]]*(#|$)'`. Do not use inline comments: `xargs` would
     pass the comment words to `apt-get`.
   - Content with group comments (carry over the reasons that are inline in the Dockerfile):
     ```
     # apt packages for the Debian 13 (trixie) build and packaging environment.
     # Consumed by packaging/linux/trixie.Dockerfile and the Linux CI job.
     # One package per line; blank lines and '#' comment lines are ignored.

     # Checkout and TLS: the CI container starts with neither.
     git
     ca-certificates

     # Build toolchain.
     build-essential
     cmake
     ninja-build

     # Qt 6.8.2 for the build and the bundled runtime.
     qt6-base-dev
     qt6-tools-dev
     qt6-tools-dev-tools
     qt6-wayland
     qt6-translations-l10n

     # X11/XTest hotkeys, selection capture, and the in-place path.
     libx11-dev
     libxtst-dev

     # Optional Secret Service build dependency (libsecret-1).
     libsecret-1-dev

     # Private session bus for tst_hotkeymanager_wayland.
     dbus

     # Headless test display; xvfb-run requires xauth.
     xvfb
     xauth

     # Packaging gates and tools: desktop-file-validate, dpkg-shlibdeps,
     # patchelf for the bundle RPATH.
     desktop-file-utils
     dpkg-dev
     patchelf

     # curl downloads the pinned AppImage tools in package.sh; file is used by
     # the release verification.
     curl
     file
     ```
   - Keep `git` and `ca-certificates` in the file even though the CI job installs them before
     checkout: the pre-checkout step cannot read the file, and the file must describe the complete
     environment of the local image as well.
   - Do not add `binutils` or `qt6-base-dev-tools`: measured, `readelf` comes through `gcc-14`
     and `dpkg-dev`, and `qtpaths6`/`qmake6` through `qt6-base-dev`. Do not add `coreutils`,
     `findutils`, `tar`, `grep`, or `sed` (essential in the base image), `patch` (unused),
     `python3`/`python3-yaml` (no CI step runs them), `libfuse2`/`squashfs-tools` (the tools are
     run with `--appimage-extract-and-run`), or `fakeroot`/`lintian`.
   - No package may appear twice: after removing comments, `sort | uniq -d` must be empty. The list
     must stay a superset of the current Dockerfile list plus `file` and `curl`.

2. Make `packaging/linux/trixie.Dockerfile` install from the shared file.
   - Keep `FROM debian:trixie`, `ENV DEBIAN_FRONTEND=noninteractive`, and `WORKDIR /src`. After the
     `ENV`, add `COPY packaging/linux/trixie-packages.txt /tmp/trixie-packages.txt` and replace the
     whole inline `apt-get install` list with:
     ```dockerfile
     RUN apt-get update \
         && grep -vE '^[[:space:]]*(#|$)' /tmp/trixie-packages.txt \
             | xargs apt-get install -y --no-install-recommends \
         && rm -rf /var/lib/apt/lists/*
     ```
   - The `COPY` source is relative to the build context. The documented command
     `docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .` already uses the
     repository root as the context, which contains the file; do not change the context to
     `packaging/linux`.
   - Keep the usage header and the note that the AppImage tools are downloaded by `package.sh`, not
     installed in the image. Drop the stale sentence that a later task adds `file` and `curl` and
     the per-package inline comments, which now live in the txt file.
   - The Dockerfile must contain no package names of its own: `grep -nE
     'build-essential|qt6-base-dev|patchelf|desktop-file-utils' packaging/linux/trixie.Dockerfile`
     returns nothing. This is what keeps the image and CI from drifting.

3. Add the `linux` job to `.github/workflows/build.yml`, after `windows` and before `release`.
   - Use the YAML from the contract with the `grep -vE` form above. The job name, container,
     `runs-on`, and `timeout-minutes: 60` are as written.
   - Put `DEBIAN_FRONTEND: noninteractive` in the job `env:`; it covers both apt steps. The default
     container user is root, which is the CI target: `tst_hotkeymanager_wayland` needs a passwd
     entry for the current UID, so do not add `--user` or `options: --user`.
   - Step order: install `git`/`ca-certificates` (checkout cannot run without them), `Check out`
     with `actions/checkout@v7`, install the list, configure, build, test, package, upload.
   - Configure with `-DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr`: trixie's Qt 6.8.2 is in
     `/usr`, and `package.sh` compares the build cache against `qtpaths6`, which also reports
     `/usr`.
   - Do not set `QT_QPA_PLATFORM` for this job. The X11 tests need the Xvfb display; the Windows job
     sets `offscreen` for its own reasons, and `tst_popupwindow`/`tst_settingsdialog` set
     `offscreen` themselves.
   - Test command exactly `timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure`:
     the timeout wraps the whole `xvfb-run`, because `xvfb-run` itself hung before starting the
     command once (task 01). `xauth` from the package list is required by `xvfb-run`.
   - Package with no `--skip-appimage`: `./packaging/linux/package.sh --build-dir build/ci
     --output-dir build/artifacts-linux`. The AppImage tools download into
     `build/linux-tools/appimage`; a network or checksum failure must fail the job.
   - Upload `qtranslate-linux-x64` from `build/artifacts-linux/*` with `if-no-files-found: error`
     and `retention-days: 14`, matching the Windows upload. The glob does not match the hidden
     `.qtranslate-staging` directory, so the staging tree is not uploaded; do not enable
     `include-hidden-files`.
   - Do not add a version/tag check to this job: the Windows check is the single gate, and two
     checks could disagree. Do not touch `on:`, `concurrency:`, `permissions`, or the `windows`
     job.
   - `actions/checkout@v7` configures the safe directory for a container job; no workaround is
     expected. Only if checkout still fails with "dubious ownership", add
     `git config --global --add safe.directory "$GITHUB_WORKSPACE"` to the pre-checkout step and
     record it; do not disable the check.

4. Extend the `release` job to publish both artifact sets.
   - Change `needs: windows` to `needs: [windows, linux]`. Keep `if: startsWith(github.ref,
     'refs/tags/v')`, `runs-on: ubuntu-latest`, `permissions: contents: write`, and the checkout
     step unchanged.
   - After the existing Windows download step, add:
     ```yaml
     - name: Download Linux artifacts
       uses: actions/download-artifact@v8
       with:
         name: qtranslate-linux-x64
         path: artifacts
     ```
   - The two download steps merge both sets into `artifacts/`; the existing
     `gh release upload artifacts/* --clobber` and the notes logic stay unchanged. Windows and
     Linux file names are distinct (see the measured names), and `SHA256SUMS-linux.txt` must not be
     renamed to `SHA256SUMS.txt`.
   - A tag whose version does not match `CMakeLists.txt` still fails in the Windows job, and the
     release job does not run because both dependencies must succeed.

5. Run the static workflow checks.
   - Parse the YAML: `ruby -ryaml -e 'YAML.load_file(".github/workflows/build.yml"); puts "ok"'`.
     PyYAML is not installed on this host; Ruby Psych is. Ruby reads `on:` as the boolean key
     `true` (YAML 1.1), which is expected and is not a syntax error; do not quote `on:`.
   - `git diff --check` is clean.
   - Review `git diff -U0 -- .github/workflows/build.yml`: the only changes are the new `linux`
     job and the release changes. The `windows` job (including its Qt 6.8.3 install, MSVC step, and
     tag/version check), the `on:` block, and `concurrency:` are byte-identical.
   - Assertions: `grep -c 'Verify tag matches' .github/workflows/build.yml` is `1`;
     `grep -n 'skip-appimage' .github/workflows/build.yml` is empty;
     `grep -c 'qtranslate-linux-x64' .github/workflows/build.yml` is `2` (upload and download);
     `grep -nE 'build-essential|qt6-base-dev|patchelf' .github/workflows/build.yml` is empty (the
     workflow reads the package file).

6. Rebuild the trixie image from the Dockerfile and verify the package file is the only source.
   - `docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .`; record the image
     ID/digest and the apt output. A failure here is a real failure; do not restore the inline
     list.
   - In the built image, confirm `git --version`, `curl --version`, `file --version`,
     `readelf --version` (binutils), `qtpaths6 --query QT_VERSION` (6.8.2), `qmake6 -query
     QT_VERSION`, `dpkg-shlibdeps --version`, and `desktop-file-validate --version` work, and that
     `command -v xvfb-run xauth` finds both, without extra installs.
   - Confirm `sort` after stripping comments has no duplicates:
     `grep -vE '^[[:space:]]*(#|$)' packaging/linux/trixie-packages.txt | sort | uniq -d` is empty.

7. Run the CI command sequence locally as root in a fresh `debian:trixie` container.
   - This is the faithful gate: fresh image, root, the new package file, and the exact commands
     from the job. Work on a copy of the tree under `/tmp/opencode/ci-linux-job` (for example
     `rsync -a --exclude .git --exclude build . /tmp/opencode/ci-linux-job/`) so no root-owned
     files appear in the repository.
   - Run (the copy is mounted writable because `package.sh` writes its tools cache under
     `build/linux-tools`):
     ```
     docker run --rm -v /tmp/opencode/ci-linux-job:/src -w /src debian:trixie bash -lc '
       set -euo pipefail
       export DEBIAN_FRONTEND=noninteractive
       apt-get update
       apt-get install -y --no-install-recommends git ca-certificates
       apt-get update
       grep -vE "^[[:space:]]*(#|$)" packaging/linux/trixie-packages.txt \
         | xargs apt-get install -y --no-install-recommends
       cmake -S . -B build/ci -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr
       cmake --build build/ci --parallel
       timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure
       ./packaging/linux/package.sh --build-dir build/ci --output-dir build/artifacts-linux
     '
     ```
   - Expected: 28/28 tests (the `tst_secretprotector` `QSKIP` for the absent session keyring is
     allowed), then `package.sh` prints the package summary and writes
     `qtranslate-<version>-linux-x86_64.tar.gz`, `qtranslate_<version>_amd64.deb`,
     `qtranslate-<version>-x86_64.AppImage`, and a three-line `SHA256SUMS-linux.txt`. Record the
     sizes and SHA256 values and run `sha256sum -c SHA256SUMS-linux.txt` in the output directory.
   - The AppImage tool download is exercised by this run because no cache exists in the copy. If
     the network is unavailable, record the exact `curl`/apt failure and mark the check as not
     performed; do not add `--skip-appimage` to make it pass.
   - Remove the copy afterwards (`sudo rm -rf /tmp/opencode/ci-linux-job` or a root container);
     the repository must stay free of root-owned files and build output (`git status` unchanged,
     `find . -user root -not -path './.git/*'` empty).

8. Report and leave the tree for the lead.
   - `git status --short` shows the task's files only: `.github/workflows/build.yml` (modified),
     `packaging/linux/trixie.Dockerfile` (modified), and `packaging/linux/trixie-packages.txt`
     (new). The lead's roadmap/journal edits, if still uncommitted, are not part of this diff. No
     documentation, journal, or map file is edited by this task; `build/` stays untracked and no
     artifact is committed.
   - Report the exact job name, the artifact names, the two checksum file names, the image digest,
     the local dry-run evidence (test count, artifact sizes/SHA256, checksum check), and the
     decisions below.
   - Record as not performed: the actual GitHub Actions run, the `v*` tag push, the release
     publication, and the Windows job execution (it is unchanged by diff and runs in CI). A local
     YAML parse and a container dry run are not a GitHub run.
   - Do not commit, push, tag, or publish. Do not edit `doc/`.

## Decisions

- One apt list in `packaging/linux/trixie-packages.txt` is consumed by the Dockerfile and by the
  CI job after checkout. `git` and `ca-certificates` stay in the file and are additionally
  installed before checkout, because the file cannot be read before the checkout and the image
  needs the complete list.
- `binutils` and `qt6-base-dev-tools` are not added explicitly: measured, `readelf` is pulled by
  `gcc-14`/`dpkg-dev` and `qtpaths6`/`qmake6` by `qt6-base-dev`; the image check in step 6 is the
  guard. `patch` is not used by `package.sh`; `python3`/`python3-yaml` are not needed by the job.
- The Linux job is a container job on `ubuntu-latest` with `debian:trixie` and `timeout-minutes:
  60`; it runs on every trigger like the Windows job, with the tag/version check left only in the
  Windows job. The release job requires both jobs.
- CI builds the AppImage by default; `--skip-appimage` is an offline-only option and never appears
  in the workflow. A failed tool download fails the job.
- `QT_QPA_PLATFORM` is not set for the Linux job; the X11 tests use the Xvfb display from
  `xvfb-run`.
- The local YAML syntax check uses Ruby Psych because PyYAML is absent on this host; the stronger
  gate is the fresh-container run in step 7 as root, which mirrors the container job.

## Done when

- `packaging/linux/trixie-packages.txt` exists and is the only apt package list: one package per
  line, `#` and blank lines allowed, no duplicates, and it contains at least `git ca-certificates
  build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-tools-dev-tools
  qt6-translations-l10n qt6-wayland libx11-dev libxtst-dev libsecret-1-dev dbus xvfb xauth
  desktop-file-utils dpkg-dev patchelf file curl`. `packaging/linux/trixie.Dockerfile` contains no
  package names and installs from the file; the image builds from it and has `readelf`, `qtpaths6`,
  `qmake6`, `dpkg-shlibdeps`, `desktop-file-validate`, `xvfb-run`, and `xauth`; its Qt is 6.8.2.
- `.github/workflows/build.yml` contains the `linux` job with `runs-on: ubuntu-latest`, `container:
  debian:trixie`, `DEBIAN_FRONTEND=noninteractive`, the pre-checkout `git`/`ca-certificates` step,
  `actions/checkout@v7`, the package-list apt step, the Release configure with
  `-DCMAKE_PREFIX_PATH=/usr`, the build, exactly `timeout 300 xvfb-run -a ctest --test-dir build/ci
  --output-on-failure`, `packaging/linux/package.sh --build-dir build/ci --output-dir
  build/artifacts-linux` without `--skip-appimage`, and the `qtranslate-linux-x64` upload with
  `if-no-files-found: error` and `retention-days: 14`.
- The `release` job has `needs: [windows, linux]`, keeps its `if:`/`permissions: contents: write`,
  downloads `qtranslate-windows-x64` and `qtranslate-linux-x64` into the same `artifacts`
  directory, and still publishes on `v*` tags with the unchanged notes logic and
  `gh release upload artifacts/* --clobber`. The Windows and Linux file names are distinct, and
  `SHA256SUMS-linux.txt` is not renamed to `SHA256SUMS.txt`.
- The Windows job, its `install-qt-action` with Qt 6.8.3, its tag/version check, the `on:`
  triggers, and `concurrency` are unchanged by diff. The Linux job adds no second tag check. The
  top-level `permissions: contents: read` and the release-only `contents: write` are unchanged.
- The workflow parses as YAML (record the exact command; the Ruby Psych check is accepted when
  PyYAML is absent), `git diff --check` is clean, and only `.github/workflows/build.yml` and
  `packaging/linux/` changed; no documentation is edited.
- The full CI command sequence run as root in a fresh `debian:trixie` container on a clean copy of
  the tree succeeds: configure, build, 28/28 tests (the known `tst_secretprotector` skip is
  allowed), and `package.sh` produces `qtranslate-<version>-linux-x86_64.tar.gz`,
  `qtranslate_<version>_amd64.deb`, `qtranslate-<version>-x86_64.AppImage`, and a three-line
  `SHA256SUMS-linux.txt` that passes `sha256sum -c`; the AppImage tool download is exercised, or
  the exact network/tool failure is recorded as not performed, never as passed.
- The actual GitHub Actions run, the `v*` tag push, the release publication, and the Windows job
  are recorded as not performed; nothing is committed, pushed, tagged, or published; the exact job
  names, artifact names, checksum file names, image digest, and local dry-run evidence are
  reported to the lead.

## Risks

- GitHub Actions container jobs run as root and the `debian:trixie` image is minimal; `git` and
  `ca-certificates` must be installed before `actions/checkout`. `actions/checkout@v7` sets the
  repository as a safe directory for container jobs (`setSafeDirectory` in `git-source-provider.ts`
  of v7.0.1), so a "dubious ownership" failure is not expected; if it appears, add
  `git config --global --add safe.directory "$GITHUB_WORKSPACE"` before the action or use the
  action's handling, do not disable the check.
- The shared package list must stay in sync with the Dockerfile. Changing one and not the other is
  the likely drift; the CI job must read the file, and the Dockerfile must not keep a second inline
  list. `qt6-wayland`, `patchelf`, `desktop-file-utils`, `dpkg-dev`, `file`, and `curl` are needed
  by the packaging children; omitting one fails only at package time.
- The `grep`/`xargs` pipeline must strip blank lines and `#` lines. An inline comment or a comment
  word passed to `apt-get` fails the job with "Unable to locate package"; use the exact
  `grep -vE '^[[:space:]]*(#|$)'` form in both the Dockerfile and the workflow.
- `readelf` (binutils) and `qtpaths6`/`qmake6` (qt6-base-dev-tools) are transitive today and
  measured present; `package.sh` names the missing package in its error if a future image drops
  them. Do not remove `build-essential` or `qt6-base-dev` from the list.
- `xvfb-run` can hang before starting the command (observed once in task 01). Keep
  `timeout 300` around the whole ctest invocation; do not run ctest without it. `xauth` is
  required by `xvfb-run`; it is in the package list.
- The fresh-container dry run runs as root. Work on a copy under `/tmp/opencode/`, never on the
  repository, so no root-owned `build/` files appear; remove the copy with `sudo rm -rf` or a root
  container afterwards. Running as the host UID instead is not faithful: `ctest` under UID 1000
  fails `tst_hotkeymanager_wayland` because `/etc/passwd` has no entry for the UID, while CI runs
  as root.
- The release job downloads both artifact sets into one directory. `SHA256SUMS-linux.txt` must
  not be named `SHA256SUMS.txt`; the artifact names `qtranslate-windows-x64` and
  `qtranslate-linux-x64` must stay distinct. `gh release upload artifacts/*` publishes everything
  in that directory; do not add unrelated files there. The hidden `.qtranslate-staging` directory
  is not matched by `build/artifacts-linux/*` and is not uploaded.
- The AppImage tool downloads happen in the Linux job; a network outage fails the job. Do not add
  `--skip-appimage` to make CI green; a missing AppImage is a failed artifact, not a warning.
- The Linux job builds only the trixie artifacts. The Astra `.deb`/tar.gz cannot be built on the
  GitHub runner; do not add a fake Astra step or claim Astra coverage from CI.
- The tag/version check is in the Windows job. Do not duplicate it in the Linux job with a
  different implementation; two checks can disagree. The release job's `needs` ensures both jobs
  succeed.
- The actual workflow run, tag push, and release publication need explicit user approval. Record
  them as not performed; a local YAML parse and a container dry run are not a GitHub run.
- The `debian:trixie` image is a moving tag. The Qt version in trixie stays 6.8.2, but record the
  image digest from the build or the run log if reproducibility is questioned; do not silently
  switch to another distro or a Qt installer.
- `package.sh` writes only under `build/`; the artifact upload path must be
  `build/artifacts-linux/*` and the Windows upload path unchanged. A wrong path with
  `if-no-files-found: error` fails the job instead of uploading an empty artifact.
- A container job executes PR code as root inside the throwaway container, including the package
  list read from the branch. There are no secrets on `pull_request` runs and the runner is
  ephemeral; do not add repository secrets to this job.

## Outcome

The Linux CI job and the shared package list are in place. Nothing is committed by the implementer,
pushed, tagged, or published; the coordinator commits the three files together with this plan and
the journal.

`packaging/linux/trixie-packages.txt` is the single apt list: 21 packages (the 21-package
Dockerfile set plus nothing new; `git` and `ca-certificates` included), one per line, with group
comments and no inline comments. `grep -vE '^[[:space:]]*(#|$)'` yields exactly 21 package words,
`sort | uniq -d` is empty, and the apt pipeline receives no comment words. `packaging/linux/trixie.Dockerfile`
keeps `FROM debian:trixie`, `ENV DEBIAN_FRONTEND=noninteractive`, and `WORKDIR /src`; it copies the
file to `/tmp/trixie-packages.txt`, installs with the same `grep | xargs` pipeline, removes the apt
lists, and contains no package names of its own. The build context stays the repository root as the
usage header documents.

`.github/workflows/build.yml` gains the `linux` job between `windows` and `release`, named
`Linux x64 (trixie)`: `runs-on: ubuntu-latest`, `container: debian:trixie`,
`timeout-minutes: 60`, `DEBIAN_FRONTEND: noninteractive`, pre-checkout `git`/`ca-certificates`,
`actions/checkout@v7`, installation from `packaging/linux/trixie-packages.txt`, Release configure
with `-DCMAKE_PREFIX_PATH=/usr`, build, exactly
`timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure`, `package.sh` without
`--skip-appimage`, and the `qtranslate-linux-x64` upload from `build/artifacts-linux/*` with
`if-no-files-found: error` and `retention-days: 14`. The job is not given `QT_QPA_PLATFORM`, a
`--user` option, a second tag check, or `include-hidden-files`, so the hidden staging directory is
not uploaded. The `release` job now has `needs: [windows, linux]` and a second
`actions/download-artifact@v8` step for `qtranslate-linux-x64` into the same `artifacts`
directory; its `if:`, `permissions: contents: write`, notes logic, and
`gh release upload artifacts/* --clobber` are unchanged. The Windows job, its Qt 6.8.3 install,
MSVC step and tag/version check, the `on:` triggers, `concurrency:`, and the top-level
`permissions: contents: read` are byte-identical to `HEAD`; `git diff -U0` touches only the new
job and the two `release` hunks.

### Local gates

- `ruby -ryaml -e 'YAML.load_file(".github/workflows/build.yml"); puts "ok"'` prints `ok` (Ruby
  Psych 3.1.2; `on:` is read as the YAML 1.1 boolean key, expected and not quoted).
- `git diff --check` is clean. Asserts: `Verify tag matches` = 1; `skip-appimage` absent;
  `qtranslate-linux-x64` = 2 (upload and download); no `build-essential`/`qt6-base-dev`/`patchelf`
  in the workflow and no package names in the Dockerfile.
- `docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .` succeeds;
  `qtranslate-trixie:latest` is image ID/digest `sha256:b17f22fb75d92426e5dac846bae2143b16c3deedb7f8ffc8de4e68580d27d642`
  on the base `debian:trixie` `sha256:51e269e6af18` (`debian@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6`).
  The copied `/tmp/trixie-packages.txt` is identical to the working tree and all 21 packages are
  installed; `git` 2.47.3, `curl` 8.14.1, `file` 5.46, `readelf` 2.44, `qtpaths6`/`qmake6` 6.8.2,
  `dpkg-shlibdeps` 1.22.22, `desktop-file-validate` 0.28, `xvfb-run`, and `xauth` work without
  extra installs; `binutils`/`qt6-base-dev-tools` are still transitive as measured.
- The exact job sequence run as root in a fresh `debian:trixie` on a copy of the tree at
  `/tmp/opencode/ci-linux-job` exits 0: Release configure, build, `timeout 300 xvfb-run -a ctest`
  gives 28/28 passed and 0 failed (11.23 s; `tst_secretprotector` passed in this run), and
  `package.sh` prints its summary. The real AppImage tool download ran in the copy (no cache):
  `linuxdeploy` `1-alpha-20251107-1`, plugin-qt `1-alpha-20250213-1`, `appimagetool` `1.9.1`, and
  `type2-runtime` `20251108`, all SHA256-matched; the log is `/tmp/opencode/ci-linux-job.log`.
  Artifacts (`0.11.0`):

  | File | Bytes | SHA256 |
  | --- | ---: | --- |
  | `qtranslate-0.11.0-linux-x86_64.tar.gz` | 28 640 068 | `79cad9be379035b48116b5ec8a983a5f3a6078527e54752379c0d118a66ac55e` |
  | `qtranslate_0.11.0_amd64.deb` | 405 948 | `8c83ae5d4f87c733fdbf5e3212c81a5aa23e4f35d6e25ebcfd4f9b51e12bef93` |
  | `qtranslate-0.11.0-x86_64.AppImage` | 39 270 904 | `f5e1c2d19058955ce4edaada02f3f5dd706884fa7c90a792e8ab61680cb89dfd` |

  `SHA256SUMS-linux.txt` has exactly the three artifact lines; `sha256sum -c` from the output
  directory reports `ЦЕЛ` (OK) for all three. The check ran on the host after the container exited,
  against the root-owned files in the bind mount; the same three SHA256 values are in the
  `package.sh` summary inside the container log, the `.deb` summary lists the expected trixie
  `Depends` (`libglib2.0-0t64`, `libxtst6 (>= 2:1.2.5)`), `Installed-Size` 1360 KiB, 21 bundled
  libraries, 17 plugins, and 13/13 Qt translations. The copy was removed afterwards;
  `find . -user root -not -path './.git/*'` is empty, `build/` stays ignored and untracked, and
  the working tree holds only the three task files plus the lead's plan/journal edits.

### Reviewer findings

No accepted remarks. The reviewer independently repeated the static asserts, the image
inspection, and the build/ctest part of the CI sequence on its own fresh `debian:trixie` copy
(28/28, 11.26 s); it did not re-run `package.sh` or the AppImage tool download, relying on the
preserved container log and the tool checks in the image. It found no defect in the job, the
release extension, the package list, the Dockerfile, or the unchanged Windows job. Its single remark — that `README.md:150`/`:163`,
`README.ru.md:152`/`:166`, and `doc/architecture.md:329`, `:1570`, `:1638` still say CI builds
Windows only and that the package list matches `trixie.Dockerfile` — is rejected for this task:
the task plan forbids documentation edits, the stage declares the documentation the lead's, and
`08-verification` checks the documentation and reports gaps; the lead should update those
statements when the stage documentation is updated.

### Not performed

- The actual GitHub Actions run. A local YAML parse, a local image build, and a container dry run
  are not a GitHub run.
- The `v*` tag push and the release publication; they need explicit user approval.
- The Windows job and its 23 tests (no Windows/MSVC/Qt here); reviewed by diff, the job is
  unchanged.
- `actions/checkout@v7`'s safe-directory handling for container jobs was not exercised outside
  GitHub. The documented expectation is that v7 sets it; no `safe.directory` workaround or
  check-disabling was added.
- CI builds no Astra artifacts; the native Astra `tar.gz`/`.deb` from child 05 stay local
  verification artifacts.

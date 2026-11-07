---
kind: task
status: todo
depends: [02-portable-tarball, 03-deb-package, 04-appimage]
---

# Linux CI job and release publication

## Task

Add the Linux job to `.github/workflows/build.yml` and extend the release job so a `v*` tag
publishes both the Windows and the Linux artifacts. The Linux job runs inside a `debian:trixie`
container (the host runner may stay `ubuntu-latest`), builds Release, runs `xvfb-run -a ctest` under
`timeout 300`, runs `packaging/linux/package.sh`, and uploads `qtranslate-linux-x64`. The existing
Windows job and its tag/version check stay unchanged and still gate the release.

Contract:

- Keep one apt package list for the local image and CI. Create
  `packaging/linux/trixie-packages.txt` (one package per line, `#` comments allowed) containing the
  full set: `git ca-certificates build-essential cmake ninja-build qt6-base-dev qt6-tools-dev
  qt6-tools-dev-tools qt6-translations-l10n libx11-dev libxtst-dev libsecret-1-dev dbus xvfb xauth
  desktop-file-utils dpkg-dev patchelf qt6-wayland file curl`. `packaging/linux/trixie.Dockerfile` installs from this
  file (copy it into the image and use `xargs apt-get install`), and the Linux CI job installs from
  the same file after checkout. Do not duplicate the list in the workflow.
- The Linux job:

  ```yaml
  linux:
    name: Linux x64 (trixie)
    runs-on: ubuntu-latest
    timeout-minutes: 60
    container: debian:trixie
    steps:
      - name: Install checkout prerequisites
        run: apt-get update && apt-get install -y --no-install-recommends git ca-certificates
      - uses: actions/checkout@v7
      - name: Install build and packaging dependencies
        run: |
          apt-get update
          grep -v '^#' packaging/linux/trixie-packages.txt | xargs apt-get install -y --no-install-recommends
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

## Done when

- `packaging/linux/trixie-packages.txt` exists and is the only apt package list for the image and
  CI; `packaging/linux/trixie.Dockerfile` still builds the same image from it, and the workflow
  installs `git`/`ca-certificates` before checkout, then the same list.
- `.github/workflows/build.yml` contains the `linux` job with the container, Release configure,
  build, `timeout 300 xvfb-run -a ctest --test-dir build/ci --output-on-failure`,
  `packaging/linux/package.sh`, and the `qtranslate-linux-x64` upload with
  `if-no-files-found: error`. The `release` job has `needs: [windows, linux]`, downloads both
  artifact sets into `artifacts`, and still publishes on `v*` tags with the existing notes logic.
- The Windows job, its Qt 6.8.3 install, its tag/version check, the `on:` triggers, and
  `concurrency` are unchanged by diff. The `release` job still requires the Windows job, so a tag
  that does not match `CMakeLists.txt` fails before publication.
- The workflow parses as YAML (for example with `python3 -c 'import yaml; yaml.safe_load(...)'`
  when PyYAML is available) and the container job's apt commands are non-interactive. A local
  container run of the same configure/build/test/package commands succeeds on trixie, or the
  missing network for the AppImage tools is recorded with the exact failure.
- `git diff --check` is clean; only the workflow and `packaging/linux/` changed; no documentation
  is edited. The exact job names, artifact names, and the not-performed GitHub run are reported to
  the lead.

## Risks

- GitHub Actions container jobs run as root and the `debian:trixie` image is minimal; `git` and
  `ca-certificates` must be installed before `actions/checkout`. If checkout complains about
  repository ownership, add `git config --global --add safe.directory "$GITHUB_WORKSPACE"` before
  the action or use the action's `safe.directory` handling; do not disable the check.
- The shared package list must stay in sync with the Dockerfile. Changing one and not the other is
  the likely drift; the CI job must read the file, and the Dockerfile must not keep a second inline
  list. `qt6-wayland`, `patchelf`, `desktop-file-utils`, `dpkg-dev`, `file`, and `curl` are needed
  by the packaging children; omitting one fails only at package time.
- `xvfb-run` can hang before starting the command (observed once in task 01). Keep
  `timeout 300` around the whole ctest invocation; do not run ctest without it. `xauth` is
  required by `xvfb-run`; it is in the package list.
- The `release` job downloads both artifact sets into one directory. `SHA256SUMS-linux.txt` must
  not be named `SHA256SUMS.txt`; the artifact names `qtranslate-windows-x64` and
  `qtranslate-linux-x64` must stay distinct. `gh release upload artifacts/*` publishes everything
  in that directory; do not add unrelated files there.
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
  image digest from the run log if reproducibility is questioned; do not silently switch to
  another distro or a Qt installer.
- `package.sh` writes only under `build/`; the artifact upload path must be `build/artifacts-linux/*`
  and the Windows upload path unchanged. A wrong path with `if-no-files-found: error` fails the
  job instead of uploading an empty artifact.
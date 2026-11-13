---
kind: task
status: done
detailed: true
---

# Drop `build-essential` from `Build-Depends`

## Task

`lintian` reports `E: build-depends-on-build-essential` for the source package built in stage 1
(`debian/control:8`; recorded in the stage-1 outcome and in the journal as an item for this
stage). `build-essential` is guaranteed by the build environment and must not be listed in
`Build-Depends`; Debian policy and lintian both say so, and the tag is an error, not a style
warning.

The build environment keeps the metapackage (`packaging/linux/trixie-packages.txt:10` lists
`build-essential`, which is correct for an image that must have `gcc`/`g++`/`make`); only the
source-package metadata changes. The rest of the `Build-Depends` field is unchanged:
`debhelper-compat (= 13)`, `cmake (>= 3.19)`, `ninja-build`, `qt6-base-dev (>= 6.5)`,
`qt6-tools-dev`, `qt6-tools-dev-tools`, `libx11-dev`, `libxtst-dev`, `libsecret-1-dev`,
`pkg-config`, `desktop-file-utils`, `dbus`, `xvfb`, `xauth`. Nothing in `CMakeLists.txt` or
`debian/rules` names `build-essential`; CMake finds the compiler through the environment.

If a clean environment turns out to need an explicit compiler dependency (it should not, because
`debhelper`/`cmake` only need a working toolchain that the environment provides), record the
finding and the decision instead of silently re-adding the entry.

## Steps

Facts checked while planning on this machine (Astra Linux 1.8.7, 2026-11-14): `dpkg-checkbuilddeps` in
the repository root is silent and exits 0 both with and without `build-essential` in the field (the
compiler is installed, so there is no `cannot determine CC system type` warning); a probe `-S` build
of the edited field produced `qtranslate_0.12.0.dsc` and `lintian` printed nothing at all (rc 0);
`fakeroot` 1.31-1.2+b4, `debhelper` 13.11.4+b7 and `lintian` 2.116.3+deb12u1b2 are installed; the
`debian:trixie` image is present; `sudo -n` works, so the gate's `install-roundtrip` really installs
and removes. `packaging/linux/trixie-packages.txt` already carries `debhelper`, `fakeroot` and
`pkg-config` (child 02/`010d882`), so the list needs no edit here.

1. **Remove the entry from `debian/control:8`.** One line changes, nothing else:
   `Build-Depends: debhelper-compat (= 13), build-essential, cmake (>= 3.19),` becomes
   `Build-Depends: debhelper-compat (= 13), cmake (>= 3.19),`. The continuation lines 9-11 and every
   other field stay byte-for-byte; no double space after the comma and no dangling comma.

   ```bash
   sed -n '8,11p' debian/control
   test "$(grep -c build-essential debian/control)" = 0 && echo clean
   dpkg-parsechangelog -S Version          # 0.12.0
   ```

   `grep -c` exits 1 when it finds nothing, so it must be read through the `test`, not checked by rc.
   Do not touch `packaging/linux/trixie-packages.txt` (the image needs the metapackage),
   `debian/changelog` or `debian/rules`.

2. **Check the build dependencies on Astra.** From the repository root, `dpkg-checkbuilddeps` must
   print nothing and exit 0. It reads the edited `debian/control`; every remaining dependency is
   installed from stage 1, and `build-essential` stays installed as a system package, so the removal
   changes nothing on this host. A `cannot determine CC system type` warning would mean the
   environment lost its compiler, not that the dependency list is wrong — do not re-add the entry to
   silence it.

3. **Build the source package in a clean copy and run `lintian`.** The working tree must not be the
   build directory: `dpkg-buildpackage` writes into the parent directory and runs `debian/rules clean`.

   ```bash
   rm -rf /tmp/opencode/be-source && mkdir -p /tmp/opencode/be-source/src
   cd /home/parsee/sync/mine/QTranslate
   tar -C "$PWD" -cf - --exclude=./.git --exclude=./build --exclude='./build-*' \
       --exclude=./out --exclude=./.claude . | tar -C /tmp/opencode/be-source/src -xf -
   ( cd /tmp/opencode/be-source/src && dpkg-buildpackage -S -us -uc )
   ls -1 /tmp/opencode/be-source
   lintian /tmp/opencode/be-source/qtranslate_0.12.0.dsc 2>&1 | tee /tmp/opencode/be-source/lintian.log
   ```

   `-us -uc` only disables signing; this is the criterion's `dpkg-buildpackage -S`. No trailing `.`:
   `dpkg-buildpackage` takes no directory argument and builds the current directory. The copy must
   exclude `build` (this working tree has one), and `debian/source/options` already excludes
   `build*`, `out` and `.claude` from the tarball. Expect `qtranslate_0.12.0.dsc`,
   `qtranslate_0.12.0.tar.xz`, `qtranslate_0.12.0_source.changes` and
   `qtranslate_0.12.0_source.buildinfo` in `/tmp/opencode/be-source`; `lintian` prints no
   `build-depends-on-build-essential` and, as in the planning probe, no other tag. The plain call is
   the baseline the stage-1 record came from; `lintian -I` also exposes info-level tags, so run it as
   well if you want the complete list. Any tag that appears is recorded in the report and `## Outcome`,
   not fixed here.

4. **Rebuild natively on Astra and gate the `.deb`.** Fresh copy the same way, then the documented
   user command from the copy:

   ```bash
   ( cd /tmp/opencode/be-astra/src \
       && DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck" dpkg-buildpackage -rfakeroot -us -uc )
   packaging/linux/verify-deb.sh /tmp/opencode/be-astra/qtranslate_0.12.0_amd64.deb
   ```

   The default build type is `full`, so the parent directory also receives the `.dsc`, the `.tar.xz`,
   the `_source`/`_amd64` `.changes`/`.buildinfo` and `qtranslate-dbgsym_0.12.0_amd64.deb` next to
   `qtranslate_0.12.0_amd64.deb`; only the `.deb` is an artifact (child 01's `package.sh` uses `-b`
   for the release set). Run the gate from the repository root without `--source-dir` (its default
   source tree is this checkout, with the edited `debian/control`) and without `sudo` (it picks up
   `sudo -n` itself). Expect `== summary: 15 checks, 15 passed, 0 failed, 0 skipped`; the gate's
   `build-deps` check runs `dpkg-checkbuilddeps` against the edited field, so a stale failure there
   means step 1 did not apply.

5. **Rebuild in `debian:trixie` and gate that `.deb`.** A fresh copy plus one container script:

   ```bash
   rm -rf /tmp/opencode/be-trixie && mkdir -p /tmp/opencode/be-trixie/src
   cd /home/parsee/sync/mine/QTranslate
   tar -C "$PWD" -cf - --exclude=./.git --exclude=./build --exclude='./build-*' \
       --exclude=./out --exclude=./.claude . | tar -C /tmp/opencode/be-trixie/src -xf -
   cat > /tmp/opencode/be-trixie/run.sh <<'EOF'
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update
grep -vE '^[[:space:]]*(#|$)' /work/src/packaging/linux/trixie-packages.txt \
    | xargs apt-get install -y --no-install-recommends
cd /work/src
echo "== dpkg-checkbuilddeps (silent expected)"
out=$(dpkg-checkbuilddeps 2>&1)
printf '%s' "$out"
[[ -z $out ]]
DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck" dpkg-buildpackage -b -us -uc
./packaging/linux/verify-deb.sh /work/qtranslate_0.12.0_amd64.deb
EOF
   timeout 1800 docker run --rm -v /tmp/opencode/be-trixie:/work -w /work/src \
       debian:trixie bash /work/run.sh > /tmp/opencode/be-trixie/run.log 2>&1
   echo "exit: $?"
   ```

   Same package list as CI, then the dependency check (must print nothing), then `-b` with `nocheck`
   (the `.deb` is a Release build without tests) and the gate on `/work/qtranslate_0.12.0_amd64.deb`.
   No `--user` (the gate's `install-roundtrip` needs root in the container) and no `--source-dir`
   (the copy is a complete source tree, so the gate's default resolves to `/work/src`). Expect exit 0,
   silence from `dpkg-checkbuilddeps`, and `15 checks, 15 passed, 0 failed, 0 skipped`. The CTest and
   AppImage steps of `02-ci-native-deb` are not repeated: they never read `Build-Depends`, and that
   task recorded them (45/45 tests, 15/15 gate).

6. **Hand off.** `git status --short` shows only `debian/control` and this `plan.md` (plus the map
   files the lead owns). Remove `/tmp/opencode/be-*` or leave it as evidence (the container output is
   root-owned, so `sudo rm -rf`). The coordinator commits `debian/control` and this plan, the lead
   commits the map; no push, tag or release.

Risks and easy mistakes:

- If the clean build fails on the compiler — it should not, because `build-essential` is installed in
  the Astra system and in the trixie image through `trixie-packages.txt` — do not bypass the check
  and do not re-add the entry quietly: record the failing command and its output, restore the line,
  and state that the removal is blocked.
- Do not fix lintian tags other than `build-depends-on-build-essential`; the criterion is to record
  them, and the planning probe saw none.
- Never run `dpkg-buildpackage` in the working tree: it writes into the parent directory and would
  leave the `.dsc`/`.deb` next to the checkout.
- The checks must read the edited `debian/control`; do not pass `--source-dir` at the stale tree and
  do not copy an old `control` into the container copy.

## Done when

- `debian/control` no longer lists `build-essential`; `dpkg-checkbuilddeps` in the repository root
  is silent and exits 0 on Astra Linux 1.8.7 and in a `debian:trixie` container with
  `packaging/linux/trixie-packages.txt` installed.
- `lintian` on the rebuilt source package (`dpkg-buildpackage -S`, then
  `lintian qtranslate_0.12.0.dsc`) has no `build-depends-on-build-essential`; every other lintian
  tag is recorded as found and not fixed here.
- The native build still succeeds on both hosts: `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck"
  dpkg-buildpackage -rfakeroot -us -uc` from a clean copy exits 0 on Astra, and the trixie
  container build from `02-ci-native-deb` exits 0; `packaging/linux/verify-deb.sh` still reports
  `15 checks, 15 passed, 0 failed` on both packages.
- Besides this plan, only `debian/control` is changed; `debian/changelog` and `debian/rules` are
  untouched, and the version stays `0.12.0`.

## Outcome

`build-essential` is no longer in `Build-Depends`, and the native package still builds and passes
the 15-check gate on both hosts.

- Changed file: `debian/control:8` only (`-build-essential, `); every other field, the
  continuation lines 9-11, `debian/changelog`, `debian/rules`, `packaging/linux/*`, `.github/`,
  the code and the tests are untouched, and the version stays `0.12.0`. The field keeps its 14
  dependencies from the plan, with no double space and no dangling comma.
- Astra: `dpkg-checkbuilddeps` in the repository root prints nothing and exits 0; the `-S` build
  from a clean copy produces `qtranslate_0.12.0.dsc` and `qtranslate_0.12.0.tar.xz`, and the
  `Build-Depends` in the `.dsc` has no `build-essential`. `lintian` on the `.dsc` prints nothing
  (rc 0); `lintian -I` adds exactly two info tags, both
  `installable-field-mirrors-source` on `debian/control:16-17` (Section and Priority in the
  binary section). They are recorded, not fixed, as the plan says.
  `build-depends-on-build-essential` is gone.
- The native Astra build from a fresh copy
  (`DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck" dpkg-buildpackage -rfakeroot -us -uc`, no
  trailing dot) exits 0, and `packaging/linux/verify-deb.sh` on the resulting
  `qtranslate_0.12.0_amd64.deb` reports `15 checks, 15 passed, 0 failed, 0 skipped`; the
  `build-deps` pass reads the edited `debian/control`, and `install-roundtrip` installs and
  removes the package through `sudo -n`.
- In `debian:trixie` (fresh copy, `packaging/linux/trixie-packages.txt` installed, `-b -us -uc`
  with `nocheck`) the container exits 0: `dpkg-checkbuilddeps` prints nothing, the build produces
  `qtranslate_0.12.0_amd64.deb`, and the gate reports `15 checks, 15 passed, 0 failed, 0
  skipped`. The CTest and AppImage steps of `02-ci-native-deb` are not repeated: they never read
  `Build-Depends`, and that task recorded them.
- `build-essential` stays in `packaging/linux/trixie-packages.txt` as the plan requires: the
  image needs the compiler, and the native builds above prove the environment still has a working
  toolchain. No line had to be restored, and no deviation from the plan was needed.
- Note: `dpkg-source` names the top-level directory of the native tarball after the build
  directory (`src/`), because the plan builds in a copy called `src`. `lintian` is silent on it;
  this is a property of the clean-copy layout, not of this change.

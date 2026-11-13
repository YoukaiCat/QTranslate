---
kind: task
status: done
detailed: true
depends: [01-debian-skeleton-and-build]
---

# Package content and dependency gates

## Task

Add a repeatable gate for the native `.deb` content and dependencies, so the package is accepted
on more than "the build exited 0". The gate is a new executable
`packaging/linux/verify-deb.sh` that takes the path to a `.deb` and checks the package against the
host that produced it (Astra or trixie). It is a verification tool; it does not change
`packaging/linux/package.sh`, the application, or the `debian/` build rules.

Every expectation must be derived from the host and the source tree, never hardcoded:
`dpkg-shlibdeps` on the extracted binary gives the expected `Depends`, `dpkg --print-architecture`
gives the expected `Architecture`, `dpkg-parsechangelog` and `CMakeLists.txt` give the expected
version, and `readelf -d` decides whether `libsecret-1-0` must be present. That is what makes the
same script usable on Astra and in `debian:trixie`; stage 2 runs it from the Linux CI job, so it
must not depend on an Astra-only path, on `packaging/linux/package.sh`, or on `sudo` (root is the
CI case, `sudo -n` the local case, and both must work).

A `3.0 (native)` package installs `usr/share/doc/qtranslate/changelog.gz`, not
`changelog.Debian.gz` (task 01); the contract below uses the native name.

The script must check at least:

- `dpkg-deb --contents`: `usr/bin/qtranslate`, `usr/share/applications/qtranslate.desktop`,
  `usr/share/icons/hicolor/256x256/apps/qtranslate.png`, and the standard
  `usr/share/doc/qtranslate/copyright` and `changelog.gz`; no `usr/bin/qt.conf`,
  `usr/share/qt6`, `usr/lib`, `usr/plugins`, or `usr/translations`, and no unexpected regular file
  (`README.Debian` is allowed but not required).
- `dpkg-deb --info`/`dpkg -I`: `Package`, `Version`, `Architecture`, `Maintainer`, and `Depends`;
  the version equals `dpkg-parsechangelog -S Version` and the CMake project version (a `+astra1`
  suffix is allowed on top of it); no maintainer scripts.
- `Depends` from `dh_shlibdeps`: extract the binary and run `dpkg-shlibdeps` with a throwaway
  `debian/control` that carries the source tree's `Build-Depends`; compare the generated
  `shlibs:Depends` with the package's `Depends`. The script must not require a hand-written
  expected list. On Astra it must see `libglib2.0-0` and no `libglib2.0-0t64`; on trixie it must
  see `libglib2.0-0t64`; the `libxtst6` floor must be whatever the host's shlibs say, not a
  constant.
- `libsecret-1-0` only when `readelf -d usr/bin/qtranslate` shows `libsecret-1.so.0`; the
  no-libsecret fallback is not a second package variant.
- `dpkg -i`/`dpkg -r` (or `dpkg -i`/`dpkg -P` in a disposable container), `dpkg -L`,
  `desktop-file-validate`, and `xvfb-run -a qtranslate --version`; remove the package afterwards
  and fail on leftovers. Without root or passwordless `sudo -n` these checks are skipped with an
  explicit warning, not silently.
- `dpkg-checkbuilddeps` in the source tree; `apt-get build-dep .` is attempted as a non-fatal,
  explicitly labelled simulation (the local-path form resolves against `debian/control` and the
  installed packages; the Astra main repositories have no active `deb-src` entries, only a
  commented one and Steam's own).
- A negative path for each check so the gate is meaningful: a missing file, an edited `Depends`,
  a bundle file, and a hardcoded dependency must each fail the script. The script owns this as a
  `--self-test` mode so the gate proves itself the same way in CI.

Run the gate on the Astra `.deb` from task 01 (it is still at
`/tmp/opencode/qtranslate_0.12.0_amd64.deb`; rebuild it from a clean copy with the exact command
if it is gone) and on a trixie `.deb` built in a disposable `debian:trixie` container: the
`qtranslate-trixie` image does not exist on this machine (`docker images` lists only
`debian:trixie`), so the run installs `packaging/linux/trixie-packages.txt` plus
`debhelper fakeroot pkg-config` into the image. The native build dependencies are installed in the
container for the check; the CI integration is stage 2.

## Steps

1. **Create `packaging/linux/verify-deb.sh`** (`#!/usr/bin/env bash`, mode 0755) with the command
   line, the result reporting, and the temp-dir lifecycle. Do not use `set -e`: the script must
   keep going after a failed check and report every failure in one run.
   - Usage:
     `packaging/linux/verify-deb.sh [--self-test] [--source-dir DIR] <package.deb>`.
     `--help` prints it and exits 0; an unknown option or a positional count other than exactly
     one prints it on stderr and exits 2.
   - Helpers `die()` (message + exit 2), `pass()`, `fail()` (increments `failures`, records the
     check id), `skip()` (records the reason for the closing warning block), `have()`
     (`command -v`).
   - One line per check, machine- and human-readable:
     `PASS <id>`, `FAIL <id>: <expected ... got ...>`, `SKIP <id>: <why>`. End with
     `== summary: <total> checks, <passed> passed, <failed> failed, <skipped> skipped` and, if
     anything was skipped, a `== skipped checks` block that repeats each reason.
   - Exit status: 0 when nothing failed (skips are allowed but never silent), 1 when at least one
     check failed, 2 for a usage error or a missing prerequisite (a tool, the `.deb`, the source
     tree).
   - `tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/verify-deb.XXXXXX")` and a `trap` on `EXIT INT TERM`
     that removes it and, if this run installed the package, removes the installed package. The
     script must never write into the source tree (CI checks `git status`).

2. **Resolve the inputs and print the host block.** Resolve the `.deb` with `realpath`, require a
   regular file, and prove it is a Debian package with `dpkg-deb --info` (a bad file exits 2, not
   1). The source tree is `--source-dir DIR` or, by default,
   `$(dirname "$(realpath "$0")")/../..`; require `debian/control`, `debian/changelog`, and
   `CMakeLists.txt` there, otherwise exit 2 and name `--source-dir`. Require
   `dpkg-deb`, `dpkg`, `dpkg-shlibdeps`, `dpkg-parsechangelog`, `readelf`, `awk`, `sed`, `grep`,
   `mktemp` (`dpkg-dev` and `binutils` in the error message). Print a header with the `.deb` path
   and `sha256sum`, the source dir, `/etc/os-release` `ID`/`VERSION_ID`,
   `dpkg --print-architecture`, the `dpkg` version line, and which install mode is active
   (`root`, `sudo -n`, or `skipped`). Extract once with `dpkg-deb -R "$deb" "$tmpdir/root"`; all
   content and metadata checks read `dpkg-deb --contents`/`--ctrl-tarfile`, not the extracted
   tree, so `DEBIAN/` never leaks into a package check.

3. **Content checks** (`contents-app-files`, `contents-docs`, `contents-bundle`,
   `contents-unexpected`), parsing `dpkg-deb --contents` by its last field:
   - `usr/bin/qtranslate` (mode `-rwxr-xr-x`), `usr/share/applications/qtranslate.desktop` and
     `usr/share/icons/hicolor/256x256/apps/qtranslate.png` (mode `-rw-r--r--`) exist.
   - `usr/share/doc/qtranslate/copyright` and `usr/share/doc/qtranslate/changelog.gz` exist, and
     `usr/share/doc/qtranslate/changelog.Debian.gz` does **not** (native package).
     `usr/share/doc/qtranslate/README.Debian` is allowed and optional (`nodoc` builds omit it).
   - None of `usr/bin/qt.conf`, `usr/share/qt6/`, `usr/lib/`, `usr/plugins/`, `usr/translations/`
     appears (match the `./` paths exactly so `usr/lib` does not also match `usr/libexec`).
   - No regular file outside the allowlist of the five required files plus the optional
     `README.Debian`; report the extra paths by name.

4. **Metadata checks** (`control-fields`, `control-version`, `control-maintainer-scripts`):
   - `Package` is `qtranslate`; `Architecture` equals `dpkg --print-architecture`;
     `Maintainer` is non-empty and equals `dpkg-parsechangelog -l "$source/debian/changelog"
     -S Maintainer`; `Description` is non-empty.
   - `Version` equals `dpkg-parsechangelog -l "$source/debian/changelog" -S Version`, and its
     upstream part (`${version%%+*}`) equals the
     `project(qtranslate VERSION X.Y.Z)` value from `CMakeLists.txt`. This accepts a later
     `0.12.0+astra1` native version without a rewrite, and still catches a version bump that
     missed one of the three files.
   - The control archive members (`dpkg-deb --ctrl-tarfile "$deb" | tar -t`, skipping `./`) are
     exactly `./control` and `./md5sums`; any `preinst`, `postinst`, `prerm`, `postrm`, `config`,
     `triggers`, `conffiles`, `shlibs`, or `symbols` is a failure. Measured on the task-01 `.deb`:
     the control archive holds `./control` and `./md5sums` only.

5. **`depends-shlibs`, the central check.** Recompute the expected `Depends` on this host and
   compare it with the package field as a set:
   - `mkdir -p "$tmpdir/shlibdeps/debian"` and copy the source tree's `debian/control` there. This
     is the critical detail: `dpkg-shlibdeps` uses the source stanza's `Build-Depends` for symbols
     files that declare `Build-Depends-Package`. Measured on this Astra host: with a hand-made
     minimal control without `Build-Depends` it prints `libqt6gui6 (>= 6.1.2)` and
     `libqt6widgets6 (>= 6.3.0)` instead of `(>= 6.5)`/`(>= 6.5)` and the comparison against the
     task-01 `.deb` fails; a copy of the real `debian/control` reproduces the package `Depends`
     exactly.
   - Run `(cd "$tmpdir/shlibdeps" && dpkg-shlibdeps -O -e"$tmpdir/root/usr/bin/qtranslate")`,
     read only stdout, take the `shlibs:Depends=` line, and require it to be non-empty. Ignore
     stderr: `dpkg-shlibdeps` warns that the analyzed binary is not installed in its package
     directory, and that warning is not an error.
   - Compare the fresh output with `dpkg-deb --field "$deb" Depends` entry by entry, splitting on
     `,` and trimming spaces, order-insensitive. Report `missing in Depends` and
     `unexpected in Depends` separately. When the only difference is the glib pair
     (`libglib2.0-0` vs `libglib2.0-0t64`), append the hint that the `.deb` was built on a
     different distribution and must be verified on the host that produced it. Do not repair,
     filter, or accept a near match: a difference is a failure.
   - The comparison is exact by design. `${misc:Depends}` is empty for this package today
     (measured: the package `Depends` is the `dpkg-shlibdeps` output verbatim); if a later change
     adds a real `misc` dependency, the message names it as unexpected instead of hiding it.

6. **`depends-glib-family` and `depends-libsecret`.** Derive both from `readelf -d` on the
   extracted binary and from the host package database, not from a fixed name:
   - If `readelf -d` lists `libglib-2.0.so.0`, detect the host family
     (`dpkg-query -W -f='${Status}' libglib2.0-0t64` reports `install ok installed` → t64,
     otherwise `libglib2.0-0`) and require that family in `Depends` and the other one absent. When
     the binary does not link glib, pass with a short "no glib dependency" note.
   - If `readelf -d` lists `libsecret-1.so.0`, `Depends` must contain an entry whose package name
     is `libsecret-1-0` (a version floor is allowed); otherwise no entry may start with
     `libsecret-1-0`. Match on the entry's package name, not on a substring of the whole field.

7. **Runtime checks on the extracted tree** (`desktop-file-validate`, `run-version`), which run
   without root:
   - `desktop-file-validate "$tmpdir/root/usr/share/applications/qtranslate.desktop"`; a missing
     `desktop-file-validate` is exit 2 with "install desktop-file-utils" (it is in
     `Build-Depends`).
   - `xvfb-run -a "$tmpdir/root/usr/bin/qtranslate" --version` must print
     `QTranslate <upstream version>` (the value from `CMakeLists.txt`, not the Debian version: a
     `+astra1` package still prints `QTranslate 0.12.0`). Run under `xvfb-run` when it is
     available and directly otherwise, noting the fallback; a missing `xvfb-run` is a `SKIP`
     warning, not a failure.

8. **`install-roundtrip`** (`dpkg -i`, `dpkg -L`, installed `desktop-file-validate`, installed
   `qtranslate --version`, `dpkg -r`, leftovers):
   - Enable it when `id -u` is 0 (`priv=()`) or `sudo -n true` succeeds (`priv=(sudo -n)`);
     otherwise print `SKIP install-roundtrip: not root and sudo -n is unavailable` plus a warning
     line with the exact command to re-run, and continue.
   - If `dpkg -s qtranslate` already reports the package installed, `SKIP` with a warning that the
     check would remove the user's installation, instead of uninstalling it.
   - `"${priv[@]}" dpkg -i "$deb"`; then `dpkg -L qtranslate` must list all five package files;
     `desktop-file-validate /usr/share/applications/qtranslate.desktop`;
     `xvfb-run -a qtranslate --version` prints `QTranslate <upstream version>`.
   - `"${priv[@]}" dpkg -r qtranslate`, then require `/usr/bin/qtranslate`,
     `/usr/share/applications/qtranslate.desktop`,
     `/usr/share/icons/hicolor/256x256/apps/qtranslate.png`, and `/usr/share/doc/qtranslate` to be
     gone. Set the "installed" flag before `dpkg -i` and clear it after `dpkg -r`, so the exit
     trap still removes the package when a later check fails.

9. **`build-deps` and `apt-build-dep`** run from the source tree:
   - `dpkg-checkbuilddeps` (in `"$source"`) must be silent and exit 0; this is a hard check.
   - `apt-get -s build-dep .` is a non-fatal, explicitly labelled simulation: exit 0 prints
     `PASS apt-build-dep` with the "would install" line; a non-zero exit prints `WARN`, not
     `FAIL`, with the APT output. The message must say that it is a dry run against the local
     `debian/control` and the installed packages, and that the Astra main repositories have no
     active `deb-src` entries, so it is not a fresh-host resolution. It must not fail the gate:
     the CI image removes `/var/lib/apt/lists`, and that would make a strict check flaky.

10. **Implement `--self-test`.** With the same `.deb` as argument, build four mutated copies in
    the temp dir and assert that each fails the intended check id:
    - `missing-file`: `dpkg-deb -R "$deb" "$tmpdir/neg-missing"`, remove
      `usr/share/applications/qtranslate.desktop`, rebuild with
      `dpkg-deb --build --root-owner-group`; expect `FAIL contents-app-files`.
    - `bundle-file`: extract, add `usr/lib/x86_64-linux-gnu/libbundle-test.so`, rebuild; expect
      `FAIL contents-bundle`.
    - `edited-depends`: extract, drop the first entry of the `Depends:` line
      (`sed -i 's/^Depends: [^,]*, /Depends: /' DEBIAN/control`), rebuild; expect
      `FAIL depends-shlibs` with `missing in Depends`.
    - `hardcoded-dep`: extract, append `, libxtst6 (>= 2:1.2.5)` to `Depends:`, rebuild; expect
      `FAIL depends-shlibs` with `unexpected in Depends`. If the host really produced that floor
      (it does not on Astra or trixie), report that the case cannot be constructed instead of
      passing it silently.
    Rebuilt copies keep a stale `DEBIAN/md5sums`; the script deliberately does not verify
    md5sums, so do not add such a check here. Print `SELFTEST PASS <case>` /
    `SELFTEST FAIL <case>: expected <id>, got <ids>` for each, never run the install roundtrip in
    this mode, and exit 1 if any case did not fail as expected. `--self-test` must not need root.

11. **Run the gate on the Astra package.** From the repository root:
    `packaging/linux/verify-deb.sh /tmp/opencode/qtranslate_0.12.0_amd64.deb` and
    `packaging/linux/verify-deb.sh --self-test /tmp/opencode/qtranslate_0.12.0_amd64.deb`. The
    install roundtrip runs through passwordless `sudo -n` on this host (measured: `sudo -n true`
    succeeds) and must remove the package afterwards. Record the full output, the `sha256sum` of
    the `.deb`, the exact `Depends` string, that `libglib2.0-0 (>= 2.12.0)` is present and
    `libglib2.0-0t64` absent, and that `libxtst6` has no floor. If the `.deb` is missing, rebuild
    it first with the exact command from task 01 step 9 (clean `rsync` copy, no trailing `.`,
    `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc"`) and record that it was rebuilt.

12. **Run the gate on a trixie package inside `debian:trixie`.** The image exists locally
    (`debian:trixie`, no `qtranslate-trixie`); a probe on 2026-11-13 ran `apt-get update` in it
    successfully, and the archive offers `debhelper` 13.24.2 and an installable `pkg-config`, so
    the build is possible. Copy the tree into the container (never build in the mounted tree) and
    run the build and the gate as root:

    ```
    mkdir -p /tmp/opencode/qtranslate-trixie
    docker run --rm \
      -v /home/parsee/sync/mine/QTranslate:/src:ro \
      -v /tmp/opencode/qtranslate-trixie:/out \
      debian:trixie bash -eux -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
          $(grep -vE "^[[:space:]]*(#|$)" /src/packaging/linux/trixie-packages.txt) \
          debhelper fakeroot pkg-config
        mkdir -p /tmp/src
        tar -C /src --exclude=.git --exclude=build --exclude="build-*" \
            --exclude=out --exclude=.claude -cf - . | tar -C /tmp/src -xf -
        cd /tmp/src
        DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" \
            dpkg-buildpackage -rfakeroot -us -uc
        dpkg-checkbuilddeps
        packaging/linux/verify-deb.sh /tmp/qtranslate_0.12.0_amd64.deb
        packaging/linux/verify-deb.sh --self-test /tmp/qtranslate_0.12.0_amd64.deb
        cp /tmp/qtranslate_0.12.0_amd64.deb /out/
      '
    ```

    Record: the image used (`debian:trixie`), the installed package list, the build time, the
    trixie `Depends` (`libglib2.0-0t64`, no `libglib2.0-0`), the `debhelper`/Qt versions actually
    seen, and the result of the install roundtrip inside the container (it runs as root, so it is
    not skipped). If apt or the network fails inside the container, that is a stop-factor to
    record with the exact command and error; do not fall back to verifying the trixie `.deb` on
    Astra, because the host-derived `Depends` would then differ by design.

13. **Check the tree and commit.** `shellcheck packaging/linux/verify-deb.sh` when it is available
    (it is on this host, not in `debian:trixie`; it is not a required gate). `git status` must
    show only the new `packaging/linux/verify-deb.sh` and this `plan.md`; no `debian/`, `src/`,
    `tests/`, `CMakeLists.txt`, `packaging/linux/package.sh`, CI, or roadmap file outside this
    node changes, and no build artifact is left behind (the script's temp dir is removed).
    Fill `## Outcome` with the evidence from steps 11–12: the exact commands, the Astra and trixie
    `Depends`, the self-test cases and their failing check ids, the host/tool versions, and any
    skipped check with its reason. Commit `packaging/linux/verify-deb.sh` and this `plan.md` as
    the task commit; the lead commits the map root, stage, and journal. Do not push.

## Done when

- `packaging/linux/verify-deb.sh` exists, is executable, prints a usage message for a wrong
  argument count (`--help` included), and returns 0 / 1 / 2 as described; it prints one
  `PASS`/`FAIL`/`SKIP` line per check plus a summary, and every `SKIP` carries a visible reason.
- The script passes on the Astra `qtranslate_0.12.0_amd64.deb` from task 01 and on a trixie
  `qtranslate_0.12.0_amd64.deb` built in `debian:trixie` with
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`.
  The Astra run shows `libglib2.0-0 (>= 2.12.0)`, no `libglib2.0-0t64`, and a plain `libxtst6`
  with no floor; the trixie run shows `libglib2.0-0t64` and no `libglib2.0-0`;
  `libsecret-1-0` is present exactly when `readelf -d usr/bin/qtranslate` shows
  `libsecret-1.so.0`.
- The package seen by the script has `usr/bin/qtranslate`,
  `usr/share/applications/qtranslate.desktop`,
  `usr/share/icons/hicolor/256x256/apps/qtranslate.png`,
  `usr/share/doc/qtranslate/copyright`, and `usr/share/doc/qtranslate/changelog.gz`; no
  `changelog.Debian.gz`, no `usr/bin/qt.conf`, `usr/share/qt6`, `usr/lib`, `usr/plugins`, or
  `usr/translations`, no unexpected file, and no maintainer script in the control archive.
- `Version` equals the `debian/changelog` version and the `CMakeLists.txt` project version,
  `Architecture` equals `dpkg --print-architecture`, and the `Depends` set equals the fresh
  `dpkg-shlibdeps` output computed from the packaged binary with the source tree's
  `Build-Depends`.
- `dpkg -i`/`dpkg -L`/`desktop-file-validate`/`xvfb-run -a qtranslate --version`/
  `dpkg -r` pass for the tested package (root in the container, `sudo -n` on Astra) and leave no
  files; `QTranslate 0.12.0` is printed. A host without root or `sudo -n` reports the skipped
  roundtrip with an explicit warning that contains the command to re-run.
- `dpkg-checkbuilddeps` is clean; `apt-get -s build-dep .` is recorded as the labelled
  simulation together with the Astra `deb-src` state, and it does not fail the gate.
- `--self-test` passes on both packages: the four mutated copies (missing file, edited `Depends`,
  bundle file, hardcoded `libxtst6 (>= 2:1.2.5)`) each fail the intended check id, and the
  construction commands are recorded in `## Outcome`.
- No application code, tests, `CMakeLists.txt`, `packaging/linux/package.sh`, CI, or `debian/`
  build rule is changed; the only changes are `packaging/linux/verify-deb.sh` and this `plan.md`.
  No license is added; no push; the coordinator commits the script and this plan, and the lead
  commits the map.

## Risks

- **The throwaway control must carry `Build-Depends`.** A minimal hand-written control makes
  `dpkg-shlibdeps` ignore the symbols files' `Build-Depends-Package` link and silently lowers the
  Qt floors (`libqt6gui6 (>= 6.1.2)`, `libqt6widgets6 (>= 6.3.0)` instead of `(>= 6.5)`); the
  check would then fail on a correct package. Copy the source tree's `debian/control`.
- **Host mismatch is a failure, not something to normalise.** The Astra and trixie packages differ
  in `libglib2.0-0` vs `libglib2.0-0t64` and in the Qt and `libxtst6` floors; verifying a foreign
  `.deb` must fail with the "verify on the build host" hint, never by filtering entries out of the
  comparison.
- **`dpkg-shlibdeps` writes warnings on stderr** ("binaries to analyze should already be installed
  in their package's directory"). Parse stdout only; treating any stderr output as failure would
  break the gate on a valid package.
- **Exact `Depends` comparison versus `${misc:Depends}`.** The package currently has no `misc`
  dependency, so exact equality holds. A future `dh_installdeb`-generated entry would be reported
  as unexpected; the message must name it so the change is visible and the check can be revisited.
- **Install/remove is destructive if the host already has `qtranslate`.** The script skips the
  roundtrip with a warning in that case; on this host the package is not installed (measured), and
  in the `debian:trixie` container it never is.
- **`apt-get -s build-dep .` is a simulation.** Astra's main repositories have no active
  `deb-src` entries (only comments and Steam's own `deb-src`), all build dependencies are already
  installed, and the CI image strips `/var/lib/apt/lists`; the check therefore reports a record,
  not a fresh-host proof, and must stay non-fatal.
- **`debian:trixie` is the only container available here**, and the trixie package must be built
  and verified inside it; the image's `debhelper` is 13.24.2 today (the map's earlier note said
  13.16), so record the versions actually seen instead of the planned ones. A missing apt/network
  in the container is a stop-factor to record, not a reason to verify the trixie `.deb` on Astra.
- **Self-test mutations rebuild the control archive.** The rebuilt copies carry a stale
  `DEBIAN/md5sums`; the script must not verify md5sums or the self-test will fail for the wrong
  reason.
- **CI reuse in stage 2.** Keep the script free of `/tmp/opencode`, Astra-only tools, and an
  unconditional `sudo`; derive the source tree from the script location so the CI checkout works
  unchanged.
- **No push.** The coordinator commits `packaging/linux/verify-deb.sh` and this `plan.md`; the
  lead commits the map root, stage, and journal.

## Outcome

`packaging/linux/verify-deb.sh` (bash, `set -u` only, mode 0755, 934 lines) is the new gate. It is
the only file added; `debian/`, `src/`, `tests/`, `CMakeLists.txt`, `packaging/linux/package.sh`,
the CI files, and the roadmap outside this node are untouched.

- Interface: `packaging/linux/verify-deb.sh [--self-test] [--source-dir DIR] <package.deb>`.
  `--help` exits 0; a wrong positional count, an unknown option, a missing tool, a missing tree or a
  file that `dpkg-deb --info` rejects exit 2. A failed check exits 1; skips never change the exit
  status but are always printed. The source tree defaults to `$(dirname "$(realpath "$0")")/../..`;
  nothing writes into it, and the `mktemp -d` directory plus any package this run installed are
  removed from an `EXIT INT TERM` trap.
- Fifteen checks: `contents-app-files`, `contents-docs`, `contents-bundle`, `contents-unexpected`,
  `control-fields`, `control-version`, `control-maintainer-scripts`, `depends-shlibs`,
  `depends-glib-family`, `depends-libsecret`, `desktop-file-validate`, `run-version`,
  `install-roundtrip`, `build-deps`, `apt-build-dep`. Both hosts end with
  `== summary: 15 checks, 15 passed, 0 failed, 0 skipped`.

Astra (task-01 package, not rebuilt, sha256 `9b4342d4…a637d8`; `astra 1.8_x86-64`, `dpkg` 1.21.22,
`debhelper` 13.11.4+b7, Qt 6.10.2, GCC 12.2):

```
packaging/linux/verify-deb.sh /tmp/opencode/qtranslate_0.12.0_amd64.deb        # exit 0
packaging/linux/verify-deb.sh --self-test /tmp/opencode/qtranslate_0.12.0_amd64.deb  # exit 0
```

- `Depends: libc6 (>= 2.34), libgcc-s1 (>= 3.0), libglib2.0-0 (>= 2.12.0),
  libqt6core6t64 (>= 6.10.2), libqt6dbus6 (>= 6.8.2), libqt6gui6 (>= 6.5),
  libqt6network6 (>= 6.7.2), libqt6widgets6 (>= 6.5), libsecret-1-0 (>= 0.7),
  libstdc++6 (>= 5), libx11-6, libxtst6` — `libglib2.0-0` present, `libglib2.0-0t64` absent,
  `libxtst6` with no floor, exactly the fresh `dpkg-shlibdeps` output.
- `readelf -d usr/bin/qtranslate` shows `libsecret-1.so.0` and `libglib-2.0.so.0`;
  `libsecret-1-0 (>= 0.7)` is therefore required and present.
- `install-roundtrip` ran through `sudo -n`: `dpkg -i`, `dpkg -L` (five files),
  `desktop-file-validate`, `xvfb-run -a qtranslate --version` → `QTranslate 0.12.0`, `dpkg -r`,
  and all four paths gone afterwards.
- `--self-test`: `4 cases, 4 passed, 0 failed, 0 skipped`. Failed ids: `contents-app-files`
  (missing-file), `contents-bundle` (bundle-file), `depends-shlibs` with `missing in Depends`
  (edited-depends), `depends-shlibs` with `unexpected in Depends` (hardcoded-dep).

trixie (`debian:trixie`, Debian 13.7; `dpkg` 1.22.22, `debhelper` 13.24.2,
`qt6-base-dev` 6.8.2+dfsg-9+deb13u2; build 23 s, whole container command ~130 s):

```
docker run --rm -v /home/parsee/sync/mine/QTranslate:/src:ro \
  -v /tmp/opencode/qtranslate-trixie:/out debian:trixie bash -eux -c '…'   # plan step 12, exit 0
```

- `tar` copy to `/tmp/src`, `DEB_BUILD_OPTIONS="parallel=24 nocheck nodoc"
  dpkg-buildpackage -rfakeroot -us -uc`, `dpkg-checkbuilddeps` (silent, exit 0), both gate runs.
  Package sha256 `3581746e…0058a1f`; two independent builds produced the same hash.
- `Depends: …, libglib2.0-0t64 (>= 2.12.0), …, libxtst6 (>= 2:1.2.5)` — `libglib2.0-0t64`
  present, `libglib2.0-0` absent, `libxtst6` with the floor from trixie's own shlibs.
- `install-roundtrip` passed as root, package removed, no leftovers.
- `--self-test`: `4 cases, 3 passed, 0 failed, 1 skipped`. `hardcoded-dep` is skipped with
  `SELFTEST SKIP hardcoded-dep: the host shlibs already require libxtst6 (>= 2:1.2.5), so this
  case cannot be constructed`, which is the fallback step 10 prescribes for exactly this host.

`dpkg-checkbuilddeps` is silent and exits 0 on both hosts (`PASS build-deps`).
`apt-get -s build-dep .` prints `0 upgraded, 0 newly installed, 0 to remove and 4 not upgraded.` on
Astra and `… 0 not upgraded.` in trixie; both runs report `PASS apt-build-dep` with the NOTE that
it is a dry run against the local `debian/control` and the installed packages, not a fresh-host
resolution, and that Astra's main repositories have no active `deb-src` entries.

What the work found:

- **`split_list` dropped the last `Depends` entry.** `< <(printf '%s' "$s" | tr ',' '\n')` left the
  final entry without a terminating newline, so `while read` discarded it in both the expected and
  the actual list and the check silently never compared `libxtst6`. Fixed with `printf '%s\n'`;
  every result above is from the fixed script.
- **trixie does ship a `libxtst6` floor.** The plan's step 10 assumed it does not, and its
  `hardcoded-dep` mutation is a no-op on trixie (`libxtst6 2:1.2.5-1`); the case is reported as a
  visible SKIP there instead of a fourth failing id, as step 10 requires. Astra has no floor, so the
  case is constructed and fails `depends-shlibs` there.
- **The throwaway control must be the real one.** `dpkg-shlibdeps` in a temp directory containing a
  copy of the source tree's `debian/control` reproduces the package `Depends` string verbatim on
  both hosts; a minimal control lowers the Qt floors and would fail a correct package.
- **`LC_ALL=C.UTF-8` when available.** Qt prints a "non-UTF-8 locale" warning on stdout that would
  otherwise break the exact `QTranslate 0.12.0` comparison; the version is read as the last
  non-empty line.
- **`--self-test` never installs.** The child runs get `VERIFY_DEB_NO_INSTALL=1`, so mutated
  packages are never installed even when the gate itself runs as root in the container.

Review fixes accepted and verified (they are part of the committed script):

- an already installed package is detected by any `ok installed` status, so a held package is
  skipped instead of removed;
- a `.deb` whose `Package` differs from the source package (for example the real
  `qtranslate-dbgsym_0.12.0_amd64.deb`) is refused with `SKIP install-roundtrip` before any
  `dpkg -i`; the `dpkg -s`/`-L`/`-r` calls and the trap use the parsed package name;
- an installed `qtranslate --version` that exits 0 with empty output now fails;
- the re-run hint is built from the actual arguments (`--source-dir` only when given, `sudo` only in
  the `sudo -n` mode);
- the glib cross-distribution hint also appears when the `libxtst6` floor differs, while the
  comparison stays exact;
- a failing `readelf -d` fails `depends-glib-family` and `depends-libsecret` instead of meaning
  "not linked"; an empty `shlibs:Depends` at exit 0 is reported as such.

`shellcheck` 0.9.0 on this host reports nothing; the trixie image has no `shellcheck`, which is not
a gate. `git status` after all runs shows only the new script and this plan.

Not done:

- The `qtranslate-trixie` image was not built; the run uses `debian:trixie` plus the packages from
  `packaging/linux/trixie-packages.txt` and `debhelper fakeroot pkg-config`, as planned.
- The CI wiring of the gate stays in stage 2.

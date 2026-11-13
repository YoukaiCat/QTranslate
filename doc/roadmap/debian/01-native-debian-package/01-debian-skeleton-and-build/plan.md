---
kind: task
status: done
detailed: true
---

# Debian skeleton and first .deb

## Task

Turn the repository into a native Debian source package and produce the first `.deb` with the
command the user uses, on Astra Linux 1.8.7:

```
DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc
```

There is no trailing `.`; `dpkg-buildpackage` 1.21.22 on Astra rejects it (`unknown option or
argument .`), and `.` is not an argument of `dpkg-buildpackage`. The command writes the `.deb`,
the `.dsc`, and the source `.tar.xz` to the parent directory.

This is the minimal connected slice: the `debian/` skeleton, the exact build command, the package
contents, `Depends` from `dh_shlibdeps`, install and remove, and the `nocheck`/`nodoc`/test
behavior. It does not change the application code or `packaging/linux/package.sh`; the release
integration is stage 2. The existing `.deb` content and metadata in `packaging/linux/package.sh`
`build_deb()` are the reference.

## Steps

1. **Create `debian/control`.** Add the source stanza and one binary package:

   - Source: `Source: qtranslate`, `Section: utils`, `Priority: optional`,
     `Maintainer: Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>`,
     `Standards-Version: 4.6.2`, `Homepage: https://github.com/youkaicat/QTranslate`,
     `Rules-Requires-Root: no`.
   - Build-Depends: `debhelper-compat (= 13)`, `build-essential`, `cmake (>= 3.19)`,
     `ninja-build`, `qt6-base-dev (>= 6.5)`, `qt6-tools-dev`, `qt6-tools-dev-tools`,
     `libx11-dev`, `libxtst-dev`, `libsecret-1-dev`, `pkg-config`, `desktop-file-utils`,
     `dbus`, `xvfb`, `xauth`.
     `qt6-tools-dev` provides `Qt6::LinguistTools`; `qt6-tools-dev-tools` provides
     `lupdate`/`lrelease` for `qt_add_translations`. `dbus` is needed by
     `tst_hotkeymanager_wayland`; `xvfb` and `xauth` are needed by `xvfb-run`.
     `libsecret-1-dev` is optional in the code but required by this package build.
   - Binary: `Package: qtranslate`, `Architecture: any`,
     `Depends: ${shlibs:Depends}, ${misc:Depends}`, `Section: utils`,
     `Priority: optional`, `Description: fast desktop translator` plus a long description that
     mentions Google Translate, DeepL, OpenAI-compatible providers, and per-user settings.
   - Do not hand-write runtime dependencies. `libqt6core6t64`, `libqt6dbus6`, `libxtst6`,
     `libsecret-1-0`, `libglib2.0-0` (Astra) and `libglib2.0-0t64` (trixie) must come from
     `dh_shlibdeps`/`${shlibs:Depends}`. The package has no maintainer scripts.

2. **Create `debian/rules`** and make it executable (`chmod 755 debian/rules`). It must use
   `dh` with the CMake buildsystem and a build directory inside the source tree:

   - `%: dh $@ --builddirectory=build/debian`.
   - Set `QTRANSLATE_BUILD_TESTS` from `DEB_BUILD_OPTIONS`: `OFF` when `nocheck` is present,
     otherwise `ON`.
   - `override_dh_auto_configure`: `dh_auto_configure -- -G Ninja -DCMAKE_BUILD_TYPE=Release
     -DCMAKE_PREFIX_PATH=/usr -DQTRANSLATE_BUILD_TESTS=$(QTRANSLATE_BUILD_TESTS)`. The default
     `dh_auto_configure` CMake build type is `None`; Release must be explicit.
   - `override_dh_auto_test`: when `nocheck` is not present, run `xvfb-run -a dh_auto_test`; when
     it is present, the target has no commands so the tests are not run. The suite includes X11
     GUI tests, so a bare `ctest` without a display must not be used.
   - `override_dh_auto_install`: `dh_auto_install --destdir=debian/qtranslate` so the package root
     is the install tree and no `debian/tmp` copy is needed.
   - `override_dh_auto_clean`: run `dh_auto_clean` and then `rm -rf build/debian`, so the local
     build directory cannot leak into the source package or the next clean build.
   - `override_dh_missing`: `dh_missing --fail-missing`, so an uninstalled CMake install file is
     a package failure rather than a silent omission.
   - Recipe lines need literal tabs; `debian/rules` must be executable.

3. **Create `debian/changelog`.** Use the native version `0.12.0` (no `-1` revision), distribution
   `unstable`, urgency `medium`, and the maintainer identity above:

   ```
   qtranslate (0.12.0) unstable; urgency=medium

     * Initial Debian packaging built with dpkg-buildpackage.

    -- Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>  Fri, 13 Nov 2026 00:00:00 +0000
   ```

   `dpkg-parsechangelog -S Version` must print `0.12.0` and match
   `project(qtranslate VERSION 0.12.0)` in `CMakeLists.txt`. A `-1` revision would be rejected by
   the `3.0 (native)` source format.

4. **Create `debian/copyright`** in machine-readable DEP-5 form. The project has no license file,
   so the copyright file must state the current terms honestly, not invent a license:

   - `Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/`,
     `Upstream-Name: QTranslate`, `Source: https://github.com/youkaicat/QTranslate`.
   - `Files: *`, `Copyright: 2026 Vladislav Mileshkin`,
     `License: All-Rights-Reserved` with the text `All rights reserved. No license is granted by
     the copyright holder.` and a `Comment` explaining that the upstream project has no license
     file and that this file records the absence of a grant. Do not add a license, a license
     file, or a license URL that the project does not have.

5. **Create `debian/source/format` and `debian/source/options`.**

   - `debian/source/format`: exactly `3.0 (native)`. The package version has no Debian revision.
   - `debian/source/options`: `tar-ignore = build`, `tar-ignore = build-*`,
     `tar-ignore = out`, and `tar-ignore = .claude`. The `.claude` directory is local tool state
     and is already in `.gitignore`; `dpkg-source` does not read `.gitignore`, so it needs the
     explicit pattern. A local `build/` directory from a previous CMake build must never enter
     the source tarball.

6. **Create `debian/.gitignore`** for the generated debhelper files:
   `/debhelper-build-stamp`, `/.debhelper/`, `/files`, `/*.substvars`, `/*.debhelper`,
   `/*.debhelper.log`, and `/qtranslate/`. The tracked sources (`debian/control`, `debian/rules`,
   `debian/changelog`, `debian/copyright`, `debian/source/*`, `debian/README.Debian`) stay
   tracked. After a build, `git status` must not list generated `debian/` files.

7. **Create `debian/README.Debian`.** Document the source build for a packager:

   - The exact command
     `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`,
     with a note that there is no trailing `.` and that Astra's `dpkg-buildpackage` 1.21.22
     rejects one.
   - The artifacts land in the parent directory: `qtranslate_0.12.0_amd64.deb`,
     `qtranslate_0.12.0.dsc`, `qtranslate_0.12.0.tar.xz`.
   - `nocheck` skips the tests, `nodoc` skips the package documentation (the copyright and
     changelog are still installed by debhelper), `parallel=N` is passed to the build.
   - Without `nocheck` the tests run under `xvfb-run`. On Astra Linux 1.8.7 the passive-grab X11
     test binaries need an `/etc/X11/trusted.d/qtranslate` entry for the `build/debian/tests/...`
     paths and `sudo /usr/bin/fly-set-execid.sh`; otherwise those tests fail on the Parsec-patched
     server. The `nocheck` build does not need this.

8. **Check the source tree before the build.** From the repository root run
   `dpkg-checkbuilddeps` (it must be silent) and `dpkg-source --print-format .` (it must print
   `3.0 (native)`). Verify `debian/rules` is executable. The `apt-get build-dep .` check is
   attempted; if the host has no `deb-src` entries, record that exact reason instead of a false
   pass.

9. **Build from a clean copy with the exact command.** Copy the tree without the local build
   directories and VCS metadata, for example:

   ```
   rsync -a --exclude .git --exclude build --exclude 'build-*' --exclude out \
       --exclude .claude ./ /tmp/opencode/qtranslate-deb/
   cd /tmp/opencode/qtranslate-deb
   DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc
   ```

   Do not pass `.`. The command must build the source package and the binary package; the
   artifacts are in `/tmp/opencode`. Record the `dpkg-source` output that lists the
   `--tar-ignore` options from `debian/source/options`. The clean copy is required: the repository
   may contain a CMake `build/` tree, and the source package must not depend on it.

10. **Verify the package contents and `Depends`.**

    - `dpkg-deb --contents qtranslate_0.12.0_amd64.deb` must show
      `usr/bin/qtranslate`, `usr/share/applications/qtranslate.desktop`,
      `usr/share/icons/hicolor/256x256/apps/qtranslate.png`, and the standard
      `usr/share/doc/qtranslate/copyright` and `changelog.Debian.gz`. With `nodoc` the
      `README.Debian` is absent; the copyright and changelog are still installed. No
      `usr/bin/qt.conf`, `usr/share/qt6`, `usr/lib`, `usr/plugins`, or `usr/translations` may
      appear.
    - `dpkg-deb --info qtranslate_0.12.0_amd64.deb` (or `dpkg -I`) must show the source version,
      `Architecture: amd64`, the maintainer, and a `Depends` produced by `dh_shlibdeps`. On Astra
      it must contain `libc6`, `libqt6core6t64`, `libqt6dbus6`, `libxtst6`, `libsecret-1-0`, and
      `libglib2.0-0`; it must not contain `libglib2.0-0t64` or a hand-written
      `libxtst6 (>= 2:1.2.5)` floor. Do not edit the control file to make this pass; the source
      of the values is `debhelper`.
    - `dpkg -i` the package (remove a previously installed `qtranslate` first if `dpkg -s` finds
      one), `dpkg -L qtranslate`, `desktop-file-validate
      /usr/share/applications/qtranslate.desktop`, and `xvfb-run -a qtranslate --version` must
      print `QTranslate 0.12.0`. Then `dpkg -r qtranslate` and confirm the three files and the
      documentation directory are gone.

11. **Run the tests without `nocheck`.** In the clean copy run the same command with
    `DEB_BUILD_OPTIONS="parallel=$(nproc) nodoc"` (no `nocheck`). `debian/rules` must build the
    test targets and run `xvfb-run -a dh_auto_test`; the expected result is 45/45. On Astra the
    passive-grab X11 binaries (`tst_hotkeymanager_x11`, `tst_hotkey_selection_x11`) need the
    trusted-client entries for their `build/debian/tests/...` paths and
    `sudo /usr/bin/fly-set-execid.sh`; prepare them before the run or record the exact commands
    and the blocker. If the Astra entry cannot be prepared, the existing 45/45 CTest at `HEAD`
    remains the test evidence; do not claim a new pass.

12. **Check the source package and the working tree.** List the `.tar.xz` contents and confirm
    there is no `build/`, `build-*`, or `out` entry and no `.git`; extract the `.dsc` with
    `dpkg-source -x` into a temporary directory. In the repository, `git status` must show only
    the new `debian/` files, `packaging/linux/verify-deb.sh` is not part of this task, and no
    `src/`, `tests/`, `CMakeLists.txt`, or `packaging/linux/package.sh` file changed. Commit the
    `debian/` files and this task's `plan.md` (with `## Outcome`) as the task commit; the lead
    commits the map root, stage, and journal. Do not push.

## Done when

- `debian/` exists with `control`, `rules`, `changelog`, `copyright`, `source/format`,
  `source/options`, `.gitignore`, and `README.Debian`; `debian/rules` is executable;
  `debian/changelog` is `0.12.0` and equals the CMake project version; `debian/source/format` is
  `3.0 (native)`.
- From the clean copy on Astra, the exact command without `.` exits 0 and produces
  `qtranslate_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc`, and `qtranslate_0.12.0.tar.xz` in the
  parent directory.
- `dpkg-deb --contents` shows the three application files and the standard Debian documentation;
  no bundle files are present; `dpkg-deb --info` shows `Depends` with `libglib2.0-0` on Astra (no
  `libglib2.0-0t64`, no `libxtst6 (>= 2:1.2.5)`), `libsecret-1-0` only when the binary links
  `libsecret-1.so.0`, and no hand-written runtime entry.
- `dpkg -i` installs, `xvfb-run -a qtranslate --version` prints `QTranslate 0.12.0`,
  `desktop-file-validate` passes, `dpkg -L` lists the expected files, and `dpkg -r` removes the
  package.
- `dpkg-checkbuilddeps` is clean; `apt-get build-dep` is recorded or its missing `deb-src`
  prerequisite is recorded; `nocheck` skips the tests, `nodoc` skips `README.Debian` while the
  copyright and changelog remain, and `parallel=N` is passed to the build.
- Without `nocheck` the suite runs under `xvfb-run`; 45/45 pass, or the Astra trusted-client
  prerequisite is recorded with the exact commands and the existing 45/45 CTest remains the gate.
- The source tarball contains no `build/`, `build-*`, or `out`; `dpkg-source -x` extracts the
  `.dsc`; `debian/copyright` states all rights reserved and adds no license.
- `git status` shows only the new `debian/` files; no application code, tests, CMake, or
  `packaging/linux/package.sh` changed; no push; the task commit contains the `debian/` files and
  this `plan.md`, and the lead commits the map root, stage, and journal.

## Risks

- **`debian/rules` syntax.** Recipe lines must start with a tab; a missing executable bit makes
  `dpkg-buildpackage` fail immediately. The `nocheck` conditional must be evaluated at make parse
  time, not inside a recipe.
- **Build type.** `dh_auto_configure` defaults the CMake build type to `None`; without the explicit
  `-DCMAKE_BUILD_TYPE=Release` the package is unoptimized.
- **`dh_missing`.** With `dh_auto_install --destdir=debian/qtranslate` an uninstalled file is a
  build failure (`--fail-missing`); do not switch to `debian/tmp` without a matching
  `debian/qtranslate.install`.
- **Astra X11 tests.** The passive-grab tests are the ones that need the trusted-client entry; the
  XTest-only tests do not. A `nocheck` build avoids the issue but does not prove the tests; the
  no-`nocheck` run is the test gate.
- **`nodoc` does not remove the copyright or changelog.** `dh_installdocs` skips `README.Debian`
  under `nodoc`, but the copyright and `changelog.Debian.gz` remain; the three-file package is the
  old manual package, not the expected native package.
- **Host-specific `Depends`.** The Astra package and the trixie package differ in the `t64` names
  and `libxtst6` floor; do not copy the trixie dependency list into the Astra build or edit the
  generated control file.
- **No license.** `debian/copyright` is not a license grant. A later license decision changes the
  copyright file and may affect distribution; it does not block this task.
- **The trailing `.`.** The user's command is without it; adding `.` makes `dpkg-buildpackage`
  1.21.22 fail before doing any work. Do not "fix" the command by adding a source directory
  argument.
- **No push.** The coordinator commits the task (`debian/` and this `plan.md`); the lead commits the
  map root, stage, and journal; the roadmap work does not push.

## Outcome

The repository is a native Debian source package and builds the first `.deb` on Astra Linux 1.8.7
with the user's command.

- Added `debian/`: `control`, `rules` (0755), `changelog`, `copyright`, `source/format`,
  `source/options`, `.gitignore`, `README.Debian`. No application code, tests, `CMakeLists.txt`,
  or `packaging/linux/package.sh` changed.
- From a clean copy (no `.git`, `build/`, `build-*`, `out/`, `.claude`),
  `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  (no trailing `.`) exits 0 and writes `qtranslate_0.12.0_amd64.deb`, `qtranslate_0.12.0.dsc`,
  and `qtranslate_0.12.0.tar.xz` to the parent directory, plus the standard
  `qtranslate-dbgsym_0.12.0_amd64.deb`, `qtranslate_0.12.0_amd64.buildinfo`, and
  `qtranslate_0.12.0_amd64.changes`.
- The package contains `usr/bin/qtranslate`, `usr/share/applications/qtranslate.desktop`,
  `usr/share/icons/hicolor/256x256/apps/qtranslate.png`,
  `usr/share/doc/qtranslate/copyright`, and `changelog.gz`; under `nodoc` it has no
  `README.Debian`, no bundle files, and no maintainer scripts. `Depends` comes from
  `dh_shlibdeps` (`libc6`, `libglib2.0-0`, `libqt6core6t64`, `libqt6dbus6`, `libqt6widgets6`,
  `libqt6gui6`, `libqt6network6`, `libsecret-1-0`, `libxtst6`, `libx11-6`, `libstdc++6`,
  `libgcc-s1`), with no `libglib2.0-0t64` and no hand-written `libxtst6` floor.
- `dpkg -i` installs, `dpkg -L` lists the files, `desktop-file-validate` passes,
  `xvfb-run -a qtranslate --version` prints `QTranslate 0.12.0`, and `dpkg -r` removes the
  package. `dpkg-checkbuilddeps` is clean, `dpkg-source --print-format .` is `3.0 (native)`,
  and `dpkg-source -x` extracts the `.dsc`.
- Without `nocheck` the same build configures `QTRANSLATE_BUILD_TESTS=ON` and runs
  `xvfb-run -a dh_auto_test`: 45/45 tests pass. `nocheck` configures
  `QTRANSLATE_BUILD_TESTS=OFF` and runs no tests. `parallel=24` reaches `ninja -j24`.
- The source tarball honors `tar-ignore` for `build`, `build-*`, `out`, and `.claude`; a sentinel
  tree with `build/sentinel`, `build-x/sentinel`, and `out/sentinel` produced a clean tarball.

What the work found:

- `dh_installdocs` in debhelper 13.11.4 installs `debian/README.Debian` before the `nodoc` check,
  so `nodoc` alone does not remove it. `debian/rules` adds `override_dh_installdocs` with
  `--exclude=README.Debian` when `nodoc` is set; the copyright and changelog are still installed.
- A `3.0 (native)` package installs `changelog.gz`, not `changelog.Debian.gz`; the plan and Risks
  above use the non-native name.
- `dh_strip` also produces a `qtranslate-dbgsym` package. The main `.deb` does not contain it; it
  is a standard debhelper output, and stage 2 must decide whether to publish or drop it.
- `dh_missing --fail-missing` is kept as planned but is a no-op with
  `--destdir=debian/qtranslate`: it does not inspect that install tree.
- The `Description` synopsis follows the plan (`fast desktop translator`); the manual
  `package.sh` package used `QTranslate - fast desktop translator`. There is no functional
  impact.
- The host has no active `deb-src` entries for the main repositories and all build dependencies
  are already installed; `apt-get -s build-dep .` reports nothing to install, so it is recorded
  as a simulation rather than a source-based check.
- The passive-grab X11 tests do not need `/etc/X11/trusted.d/qtranslate` entries under Xvfb; the
  no-`nocheck` build passed 45/45 without them. `README.Debian` still documents the entries for
  a build on a real Parsec X server.
- `dpkg-source` names the native tarball's top-level directory after the clean-copy directory
  (`qtranslate-deb`); the package and artifact names are `qtranslate_0.12.0`.

Not done:

- `packaging/linux/package.sh`, the CI/release integration, and the `+astra1` version stay in
  stage 2; `debian/README.Debian` lists only the three artifacts the plan requires and does not
  mention `dbgsym`, `.buildinfo`, and `.changes`.
- The build without `nodoc` was not run; `nodoc` is the user's command and the plan's gate.
- The `changelog.Debian.gz` wording and the `Description` difference are recorded here, not
  changed in the plan.
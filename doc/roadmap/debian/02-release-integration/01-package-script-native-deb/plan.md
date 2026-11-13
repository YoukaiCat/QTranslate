---
kind: task
status: in-progress
detailed: true
---

# Native `.deb` from `dpkg-buildpackage` in `package.sh`

## Task

`packaging/linux/package.sh` still assembles the `.deb` by hand: `build_deb()`
(`packaging/linux/package.sh:747`) copies the CMake install tree into `$deb_root` (`:753`),
writes `DEBIAN/control` from a here-document (`:866`), runs `dpkg-shlibdeps` (`:814`) and calls
`dpkg-deb --build` (`:884`). Since stage 1 the repository is a native Debian source package
(`debian/`, `debian/rules`, a `3.0 (native)` source format), so the release `.deb` must be the
`dpkg-buildpackage` output, not a hand-staged tree.

Decision: **the script calls `dpkg-buildpackage` itself**, not the CI job. The script stays the
single entry point for the whole artifact set on both targets: `--target trixie|astra`,
`--build-dir`, `--output-dir`, `--version` and `--skip-appimage` keep their meaning,
`write_checksums()` (`:1104`) still sees the `.deb` it lists, and the Astra flow stays one
command on Astra. The CI job (`02-ci-native-deb`) therefore keeps one `Package` step.

The build runs in a clean copy of the tree at `$stage/deb-src` (`$stage` is
`<output-dir>/.qtranslate-staging`, `:450`), so `dpkg-buildpackage` writes
`qtranslate_0.12.0_amd64.deb`, `qtranslate-dbgsym_0.12.0_amd64.deb`, the `.changes` and the
`.buildinfo` into `$stage` (the copy's parent). Only `qtranslate_<version>_amd64.deb` is copied
into `$output_dir`; nothing is written into the working tree or into the repository's parent
directory. The copy is the same "copy the project and run `dpkg-buildpackage`" flow the user
documented, and it keeps `debian/rules` (`build/debian`) out of the release tree.

User decisions recorded in the journal (2026-11-14) and applied here:

- **Drop `+astra1`.** Both the Astra and the trixie `.deb` are `0.12.0`; the suffix is for
  patched or backported builds. `SHA256SUMS-linux-astra.txt` still separates the Astra set.
- **Do not publish `qtranslate-dbgsym`.** It is a normal build output and stays in `$stage`; it
  never reaches `$output_dir` or a checksum file.

Consequence to state in the usage text: after dropping `+astra1` the trixie and the Astra `.deb`
have the same file name, so the two sets must be built into different output directories.

Tests are not run twice. `debian/rules:16-19` runs the 45 tests only without `nocheck`; the script
builds the `.deb` with `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck"`, so the `.deb` build is a
Release build without the test targets. The test gate stays the Release CTest run of the build
directory (`timeout 300 xvfb-run -a ctest --test-dir build/ci` in CI, 45/45 on Astra). The user's
own documented command without `nocheck` still runs the tests — that behaviour is not touched.

## Steps

1. `check_repo_root()` (`packaging/linux/package.sh:177`) must export the absolute tree root, for
   example `repo_root=$(pwd -P)`, because the copy step and the `$output_dir` exclusion need it;
   keep the existing `CMakeLists.txt`/`src/` guard.

2. `resolve_version()` (`:293-313`): replace the `+astra1` branch with the settled naming —
   `deb_version=$version` for both targets, `bundle_name` and `checksums_file` stay per target
   (`qtranslate-$version-linux-x86_64[-astra]`, `SHA256SUMS-linux[-astra].txt`). Then read the
   package version from the source package and refuse to disagree with it:

   ```
   changelog_version=$(dpkg-parsechangelog -SVersion)
   [[ $changelog_version == "$version" ]] \
       || die "debian/changelog is $changelog_version but the artifact version is $version; update debian/changelog or pass --version $changelog_version"
   ```

   The `.deb` name and version now come from `debian/changelog`; `--version` only chooses the
   tarball/AppImage name and must match it. Keep the `--version X.Y.Z` help text and add that
   requirement there.

3. Replace `build_deb()` (`:747-935`) with the native build. Delete the manual staging:
   `deb_root`, the `DEBIAN/control` here-document, the file/mode/bundle checks, the
   `dpkg-shlibdeps` call, the `Installed-Size` computation, `dpkg-deb --build`, the control-field
   and `--contents` repetitions, the maintainer-script check, and the `install_sha == staged_sha`
   assertion (`:806-811`, `:887-890`) — the `.deb` is now an independent build, not a copy of the
   CMake install tree. `verify-deb.sh` (stage 1) owns the content, `Depends` and control checks;
   do not copy its 15 checks into the script. The new function:

   - `local deb_src="$stage/deb-src"`; `mkdir -p "$deb_src"`.
   - Copy the tree with the same exclusions the source package uses (`debian/source/options`):
     `.git`, `build`, `build-*`, `out`, `.claude`. `tar` is already a required tool:
     `tar -C "$repo_root" -cf - --exclude=./.git --exclude=./build --exclude='./build-*' --exclude=./out --exclude=./.claude . | tar -C "$deb_src" -xf -`.
     Additionally exclude `$output_dir` when it lies inside `$repo_root` (compute both absolute;
     `--output-dir build/artifacts-linux` is the default and is covered by `build`, but
     `--output-dir` may point anywhere). Without that exclusion the copy would recurse into
     `$stage` itself.
   - Build from the copy: `( cd "$deb_src" && DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck ${DEB_BUILD_OPTIONS-}" dpkg-buildpackage -b -us -uc )`.
     `-b` skips the source `.tar.xz`/`.dsc` (the artifacts are not part of the release set),
     `-us -uc` skips signing. Do not pass `-r`: `debian/control:7` has
     `Rules-Requires-Root: no`, so the build needs neither root nor fakeroot.
   - `native_deb="$stage/qtranslate_${deb_version}_amd64.deb"` (the copy's parent is `$stage`);
     `[[ -f $native_deb ]] || die`.
   - `deb_artifact="$output_dir/qtranslate_${deb_version}_amd64.deb"`; `cp` it there; set
     `deb_size=$(stat -c %s "$deb_artifact")`,
     `deb_depends=$(dpkg-deb -f "$deb_artifact" Depends)`,
     `deb_installed_size=$(dpkg-deb -f "$deb_artifact" Installed-Size)`.
   - Keep a short self-check on the copied file (the gate does the rest):
     `dpkg-deb -f` says `Package: qtranslate`, `Version: $deb_version`, `Architecture: amd64`;
     `[[ ! -e $output_dir/qtranslate-dbgsym_${deb_version}_amd64.deb ]]`; the `.changes` and
     `.buildinfo` are not copied.
   - `rm -rf "$deb_src"` after the copy so the run leaves only the staging leftovers; the
     dbgsym and the `.changes`/`.buildinfo` stay in `$stage` and are removed by the next
     `prepare_stage` (`:454`) — they are never artifacts.

4. `check_tools()` (`:238-291`): drop `dpkg-shlibdeps:dpkg-dev`; add `dpkg-buildpackage:dpkg-dev`
   and `dpkg-parsechangelog:dpkg-dev`; keep `dpkg-deb:dpkg` (the short self-check) and
   `desktop-file-validate` (used by `install_tree`, `:475`); add `nproc:coreutils` for step 3.

5. `usage()` (`:66-115`): the astra artifact list becomes
   `qtranslate_<version>_amd64.deb`; remove the `+astra1` sentence (`:80`) and the manual `.deb`
   wording; describe the native build: the `.deb` is built by `dpkg-buildpackage` from a clean copy
   under `<output-dir>/.qtranslate-staging` with `DEB_BUILD_OPTIONS=parallel=N nocheck`, the
   `dbgsym`/`.changes`/`.buildinfo` stay in the staging directory and are not artifacts, and the
   trixie and astra `.deb` files have the same name so the two sets need different output
   directories. Update the required-tools list (`:92-95`): `dpkg-buildpackage` and
   `dpkg-parsechangelog` instead of `dpkg-shlibdeps`.

6. Header comment (`:1-12`): say the `.deb` is the native source-package build, not a staging of
   the install tree; the tarball/AppImage still come from the CMake install tree.

7. `pack_tarball()` (`:728-745`): update the comment at `:733-735` — the archive is packed before
   `build_deb`, and the native build's copy lives in a subdirectory of `$stage`, so the top-level
   check (`:738`) still holds. No behavioural change.

8. `print_summary()` (`:1130-1166`): `depends` and `installed size` come from the `dpkg-deb -f`
   values set in step 3; `deb:`/`deb size:`/`deb sha256:` keep their labels. Die with a clear
   message if `Depends` or `Installed-Size` is empty.

9. `main()` (`:1168-1198`): keep the order — `prepare_stage`, install/bundle steps, `pack_tarball`,
   `build_deb`, `build_appimage` (trixie only, `:172-174`), `write_checksums`, `print_summary`.
   `build_deb` must run after `prepare_stage` (it uses `$stage`) and before `write_checksums`
   (`:1106-1119` lists `qtranslate_${deb_version}_amd64.deb`).

10. Verify on this machine (Astra Linux 1.8.7, `patchelf`, `qtpaths6`, `dpkg-buildpackage`
    1.21.22, `debhelper` 13.11.4 all present; `build/linux-release` is a Release CMake tree):

    ```
    cmake --build build/linux-release --parallel      # the install tree must be current
    rm -rf /tmp/opencode/astra-native
    ./packaging/linux/package.sh --target astra --build-dir build/linux-release \
        --output-dir /tmp/opencode/astra-native
    ls -1 /tmp/opencode/astra-native
    ```

    Expected: `qtranslate-0.12.0-linux-x86_64-astra.tar.gz`, `qtranslate_0.12.0_amd64.deb`,
    `SHA256SUMS-linux-astra.txt`; no AppImage, no `+astra1`, no `dbgsym`, no `.changes`.
    Then:

    ```
    packaging/linux/verify-deb.sh /tmp/opencode/astra-native/qtranslate_0.12.0_amd64.deb
    ( cd /tmp/opencode/astra-native && sha256sum -c SHA256SUMS-linux-astra.txt )
    dpkg-deb -f /tmp/opencode/astra-native/qtranslate_0.12.0_amd64.deb Package Version Architecture Depends
    ```

    Expect `15 checks, 15 passed, 0 failed`, a matching `sha256sum -c`, `Version: 0.12.0`, and a
    `Depends` that `verify-deb.sh` (`depends-shlibs`) confirms is the host's fresh
    `dpkg-shlibdeps` output. A second run into the same output directory must also succeed.

11. Negative and static checks:

    ```
    ./packaging/linux/package.sh --target astra --build-dir build/linux-release \
        --output-dir /tmp/opencode/astra-native --version 9.9.9   # exit != 0, names debian/changelog
    ./packaging/linux/package.sh --help                            # native artifact names, no +astra1
    bash -n packaging/linux/package.sh
    shellcheck packaging/linux/package.sh                          # 0.9.0 on this host, clean
    ```

12. `git status --short` must show only `packaging/linux/package.sh` and this `plan.md` (plus the
    map files the lead owns). `git diff --stat`. The staging directory stays under the output
    directory and is not tracked; nothing new appears in the repository parent.

Risks and easy mistakes:

- **The copy and `$output_dir`.** If `--output-dir` is inside the repository but not under
  `build/`, the tar copy must exclude it explicitly, otherwise the copy contains the staging
  directory it is being written into.
- **`debian/rules clean` in the copy.** `dpkg-buildpackage` runs `debian/rules clean`
  (`:10-11`, `override_dh_auto_clean`), which writes only inside the copy; never run
  `dpkg-buildpackage` in the real tree from the script.
- **`dpkg-checkbuilddeps` runs implicitly.** `dpkg-buildpackage` runs it unless `-d` is given; the
  host must have the `Build-Depends` installed (Astra does; the trixie list is child 02). Keep the
  check — it is a free gate — and do not pass `-d`.
- **`nocheck` is deliberate.** A reviewer may expect the script to run the tests; the reasoning is
  in `## Task` and must be repeated in the script's usage text so the choice is visible in the
  code, not only in this plan.
- **Two independent binaries.** The `.deb` binary comes from the package build (`dh_strip` moves
  the symbols into the unpublished `dbgsym`); the tarball/AppImage binary comes from
  `build/linux-release`. They are no longer byte-identical, which the old script asserted. Do not
  reintroduce that assertion; the `.deb` gate is `verify-deb.sh`, the bundle gate is
  `set_rpaths`/`check_ldd`.
- **Astra only here.** The trixie host path, the AppImage and the container run are
  `02-ci-native-deb`'s gate; do not try to run `--target trixie` on Astra (`check_host`, `:211-234`
  refuses it).

## Done when

- `packaging/linux/package.sh` has no manual `.deb` staging:
  `grep -nE 'deb_root|DEBIAN/control|dpkg-shlibdeps|dpkg-deb --build|astra1' packaging/linux/package.sh`
  finds nothing; the `.deb` is produced by `dpkg-buildpackage` in a copy under
  `<output-dir>/.qtranslate-staging` and copied from there into `$output_dir`.
- The Astra run of step 10 exits 0 on Astra Linux 1.8.7 and writes exactly
  `qtranslate-0.12.0-linux-x86_64-astra.tar.gz`, `qtranslate_0.12.0_amd64.deb` and
  `SHA256SUMS-linux-astra.txt`; the output directory has no AppImage, no `qtranslate-dbgsym`,
  no `.changes` and no `.buildinfo`.
- `packaging/linux/verify-deb.sh` on that `.deb` ends with
  `15 checks, 15 passed, 0 failed, 0 skipped`; `sha256sum -c SHA256SUMS-linux-astra.txt` passes;
  `dpkg-deb -f` shows `Package: qtranslate`, `Version: 0.12.0`, `Architecture: amd64`.
- `--version` that does not match `debian/changelog` exits non-zero with a message naming both
  versions; `--help` lists `qtranslate_<version>_amd64.deb` for the astra target, names the native
  build and the separate output-directory requirement, and has no `+astra1`.
- `bash -n` passes and `shellcheck packaging/linux/package.sh` reports nothing.
- `src/`, `tests/`, `CMakeLists.txt`, `debian/`, `packaging/linux/verify-deb.sh`, the CI files,
  the tarball name, the AppImage name and the `SHA256SUMS-linux.txt` / `SHA256SUMS-linux-astra.txt`
  names are unchanged; the `.deb` name is `qtranslate_<version>_amd64.deb` on both targets.
- Besides this plan, only `packaging/linux/package.sh` is changed.

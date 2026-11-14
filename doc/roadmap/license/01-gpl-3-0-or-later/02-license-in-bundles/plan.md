---
kind: task
status: done
detailed: true
depends: [01-license-docs-and-metadata]
---

# Ship the license with the bundles

## Task

The license text must travel with the binaries, not only with the source. The Windows ZIP and
installer and the Linux portable `tar.gz`/AppImage are built from the CMake install tree; the
native `.deb` already installs `debian/copyright`. Add the `LICENSE` file to the CMake install
tree at `share/doc/qtranslate/LICENSE` and verify that every bundle receives it.

This is a build/packaging change in `CMakeLists.txt` plus the content allowlist of
`packaging/linux/verify-deb.sh` (the gate rejects files outside its allowlist, so the new
`usr/share/doc/qtranslate/LICENSE` must be admitted or the gate fails on a correct package; in the
`.deb` Debian's `dh_compress` stores it as `LICENSE.gz`, which is the name the gate uses); it does
not change the application, the tests, the runtime behavior or the artifact names. It is executed by
a coordinator (the install rule is build configuration), after the documentation task.

## Steps

1. **CMake install rule.** In `CMakeLists.txt`, next to the common install rules (after the
   `install(TARGETS qtranslate ...)` block, before the Linux/Windows blocks), add:

   ```cmake
   install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qtranslate)
   ```

   The source file is the repository-root `LICENSE`; `${CMAKE_INSTALL_DATADIR}` is `share` on all
   targets. This puts the file at `share/doc/qtranslate/LICENSE` in the install tree.

2. **Check the install tree.** Configure and install a Release tree into a temp prefix:

   ```bash
   cmake -S . -B build/license-check -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/license-check
   rm -rf /tmp/opencode/license-install
   cmake --install build/license-check --prefix /tmp/opencode/license-install
   test -f /tmp/opencode/license-install/share/doc/qtranslate/LICENSE
   head -2 /tmp/opencode/license-install/share/doc/qtranslate/LICENSE
   ```

   The file must be the GPL-3 text and no other installed file may move. The test build is
   `build/license-check`; remove it afterwards if it is not needed.

3. **Linux portable bundle and AppImage.** Build the actual artifacts with
   `packaging/linux/package.sh` on Astra into `/tmp/opencode/license-bundle` (use the existing
   `build/linux-release`, which is the documented local path; the host is Astra, so the target is
   `astra`, which names the archive `qtranslate-0.12.0-linux-x86_64-astra.tar.gz` and skips the
   AppImage) and inspect the tar:

   ```bash
   ./packaging/linux/package.sh --target astra --build-dir build/linux-release \
       --output-dir /tmp/opencode/license-bundle
   tar -tzf /tmp/opencode/license-bundle/qtranslate-0.12.0-linux-x86_64-astra.tar.gz \
       | grep 'qtranslate-0.12.0-linux-x86_64-astra/share/doc/qtranslate/LICENSE'
   ```

   The path must exist; the bundle top level must stay the single `qtranslate-...` directory and
   the artifact names must not change. The AppImage is built from the same install tree; its
   `--appimage-extract` check (or the `AppDir/usr/share/doc/qtranslate/LICENSE` path in the
   staging directory) must show the same file. If the AppImage tool run is too expensive, record
   that the AppDir copy was checked and the AppImage is built from it. On the `astra` target the
   AppImage is not built at all; the record is the static path (`build_appimage()` copies the
   install tree into `AppDir/usr/`) and the CI trixie build.

4. **Native `.deb`.** Build the native package from a clean copy with the user's command
   (`DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`,
   no trailing `.`) and check:

   ```bash
   dpkg-deb -c qtranslate_0.12.0_amd64.deb | grep 'usr/share/doc/qtranslate/'
   ```

   `usr/share/doc/qtranslate/copyright` must contain `GPL-3+` (the task-01 file) and
   `usr/share/doc/qtranslate/LICENSE.gz` must be present: `dh_compress` gzips the 35 KiB `LICENSE`
   (`copyright` is exempt). `dpkg -i`/`-r` is covered by the gate. The `.deb` that `package.sh`
   builds from the same clean copy (`-b`, `nocheck`, `README.Debian` kept) is the release artifact
   and satisfies this check; a separate `nodoc` build is not required.

5. **Gate allowlist.** `packaging/linux/verify-deb.sh` currently requires `copyright` and
   `changelog.gz` and rejects any regular file outside a fixed allowlist (`contents-docs`,
   `contents-unexpected`). Add `./usr/share/doc/qtranslate/LICENSE.gz` to the required docs list
   and to the allowlist, and update the `contents-docs` message. Run
   `packaging/linux/verify-deb.sh --self-test <deb>`; the four mutations must still fail the
   intended checks and the normal run must be 15/15.

6. **Windows path (static).** There is no MSVC on this host; do not claim a Windows build. Check
   that the install tree goes to `build/dist` (`package.ps1` runs `cmake --install ... --prefix
   $distPath`) and that `Compress-Archive -Path (Join-Path $distPath "*")` packs the new
   `share/doc/qtranslate/LICENSE`; the Inno Setup `[Files]` section uses the same `SourceDir`
   recursively, so the installed program also receives it. Record the Windows result as not
   performed.

7. **Gates and commit.** Run the 45 Linux tests on the final tree
   (`xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`) and
   `packaging/linux/verify-deb.sh` on the rebuilt `.deb`; `git status` must show only
   `CMakeLists.txt`, `packaging/linux/verify-deb.sh` and this `plan.md` (plus the lead's map
   files). Commit those three files with the task `plan.md` carrying `## Outcome`; the lead commits
   the map; no push.

## Done when

- `CMakeLists.txt` installs `LICENSE` at `share/doc/qtranslate/LICENSE`; the install tree, the
  Linux `tar.gz` and the `.deb` carry the file, and the AppImage's AppDir copy is confirmed or the
  AppImage build is recorded as not run with the reason.
- The artifact names and the bundle top-level layout are unchanged; the `.deb` still installs
  `copyright` plus `LICENSE.gz`, with `GPL-3+` in the copyright.
- `verify-deb.sh` admits `usr/share/doc/qtranslate/LICENSE.gz`, is 15/15 on the rebuilt package and
  its `--self-test` still passes; the 45 Linux tests pass; no application code changed.
- The Windows installer/ZIP path is checked statically and recorded as not built (no MSVC).
- The coordinator commits `CMakeLists.txt`, `packaging/linux/verify-deb.sh` and this plan; the lead
  commits the map; no push.

## Risks

- **`dh_installdocs` and the extra file.** A `LICENSE` under `usr/share/doc/qtranslate` is a regular
  file next to `copyright` and `changelog.gz`; `dh_missing` with `--destdir` is a no-op, but check
  `dpkg-deb -c` and `lintian` anyway. If a Debian rule complains, move the file to
  `share/doc/qtranslate/LICENSE` only in the non-Debian install path rather than dropping it.
  Actual: `dh_compress` gzips it to `LICENSE.gz` and `lintian` reports no error; the gate requires
  `LICENSE.gz`, see `## Outcome`.
- **The Windows build is not available.** Never report the Windows installer as built or tested;
  the static path check is the evidence.
- **`build/license-check`** is a temporary tree; do not add it to the repository or leave it in a
  place the source package would pick up (`build/` is already ignored and excluded).
- **Release approval.** No push, tag or release from this task.

## Outcome

Done as one commit with `CMakeLists.txt` and `packaging/linux/verify-deb.sh`; no push.

- `CMakeLists.txt` installs `LICENSE` at `${CMAKE_INSTALL_DATADIR}/doc/qtranslate`. The install tree
  from the existing `build/linux-release` Release tree (the documented local path; the separate
  `build/license-check` tree was not needed) holds exactly `bin/qtranslate`,
  `share/applications/qtranslate.desktop`, `share/icons/hicolor/256x256/apps/qtranslate.png` and
  `share/doc/qtranslate/LICENSE`; the installed file is byte-identical to the repository `LICENSE`
  (35149 bytes, GPL-3 header).
- Linux: `packaging/linux/package.sh --target astra` on Astra Linux 1.8.7 (Qt 6.10.2) produced
  `qtranslate-0.12.0-linux-x86_64-astra.tar.gz` with the single top-level
  `qtranslate-0.12.0-linux-x86_64-astra/` directory and
  `share/doc/qtranslate/LICENSE` (sha256 equal to the repository file), and the native
  `qtranslate_0.12.0_amd64.deb`. The artifact names and the bundle layout are otherwise unchanged.
- The `.deb` contains `usr/share/doc/qtranslate/LICENSE.gz`, `copyright`, `changelog.gz` and
  `README.Debian`; `copyright` carries `GPL-3+` and `LICENSE.gz` decompresses to the GPL-3 text. The
  plan's `LICENSE` wording was wrong for a `.deb`: `dh_compress` (debhelper 13.11.4) gzips regular
  files over 4 KiB under `usr/share/doc`, `copyright` is exempt, so the shipped name is
  `LICENSE.gz`. The `.deb` that `package.sh` builds from the clean copy (`-b`, `nocheck`,
  `README.Debian` kept) is the release artifact; a separate `nodoc` build was not needed.
- `verify-deb.sh` requires and admits `./usr/share/doc/qtranslate/LICENSE.gz`; on the rebuilt package
  the gate is 15/15 on Astra and `--self-test` is 4/4. `lintian` reports only the pre-existing
  `no-manual-page` warning; the license file raises the informational `extra-license-file`, which is
  not shown by default and is not a gate failure.
- AppImage: not built, because the host is Astra and the `astra` target skips it by design; the
  AppImage is built from the same install tree (`build_appimage()` copies `"$install_root/."` into
  `AppDir/usr/`, `packaging/linux/package.sh:882`) and is built by CI on trixie.
- Windows: not built (no MSVC). Static path: `package.ps1:123` installs the tree into `build/dist`,
  `package.ps1:153` packs `dist/*` into the ZIP, and `qtranslate.iss:66` installs `SourceDir\*` with
  `recursesubdirs`, so `dist/share/doc/qtranslate/LICENSE` reaches both the ZIP and the installer.
- 45/45 Linux tests pass (`xvfb-run -a ctest --test-dir build/linux-release`); no application code,
  tests or artifact names changed.

Follow-up outside this commit: the bundle-content enumerations in `doc/architecture.md` (§3.3, §3.4,
§9.1), `doc/handoff.md` and the `packaging/linux/package.sh` header still name the old contents; the
task's commit scope is the three files above, so they are left for the stage acceptance.

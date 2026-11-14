---
kind: task
status: in-progress
detailed: true
depends: [01-license-docs-and-metadata]
---

# Ship the license with the bundles

## Task

The license text must travel with the binaries, not only with the source. The Windows ZIP and
installer and the Linux portable `tar.gz`/AppImage are built from the CMake install tree; the
native `.deb` already installs `debian/copyright`. Add the `LICENSE` file to the CMake install
tree at `share/doc/qtranslate/LICENSE` and verify that every bundle receives it.

This is a build/packaging change in `CMakeLists.txt` only; it does not change the application, the
tests, the runtime behavior or the artifact names. It is executed by a coordinator (the install
rule is build configuration), after the documentation task.

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
   cmake -S . -B build/license-check -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_TYPE=Release
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
   `build/linux-release`, which is the documented local path) and inspect the tar:

   ```bash
   ./packaging/linux/package.sh --build-dir build/linux-release --output-dir /tmp/opencode/license-bundle
   tar -tzf /tmp/opencode/license-bundle/qtranslate-0.12.0-linux-x86_64.tar.gz \
       | grep 'qtranslate-0.12.0-linux-x86_64/share/doc/qtranslate/LICENSE'
   ```

   The path must exist; the bundle top level must stay the single `qtranslate-...` directory and
   the artifact names must not change. The AppImage is built from the same install tree; its
   `--appimage-extract` check (or the `AppDir/usr/share/doc/qtranslate/LICENSE` path in the
   staging directory) must show the same file. If the AppImage tool run is too expensive, record
   that the AppDir copy was checked and the AppImage is built from it.

4. **Native `.deb`.** Build the native package from a clean copy with the user's command
   (`DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`,
   no trailing `.`) and check:

   ```bash
   dpkg-deb -c qtranslate_0.12.0_amd64.deb | grep 'usr/share/doc/qtranslate/'
   ```

   `usr/share/doc/qtranslate/copyright` must contain `GPL-3+` (the task-01 file) and
   `usr/share/doc/qtranslate/LICENSE` must be present. `packaging/linux/verify-deb.sh` on the
   package must stay 15/15; `dpkg -i`/`-r` is covered by the gate.

5. **Windows path (static).** There is no MSVC on this host; do not claim a Windows build. Check
   that the install tree goes to `build/dist` (`package.ps1` runs `cmake --install ... --prefix
   $distPath`) and that `Compress-Archive -Path (Join-Path $distPath "*")` packs the new
   `share/doc/qtranslate/LICENSE`; the Inno Setup `[Files]` section uses the same `SourceDir`
   recursively, so the installed program also receives it. Record the Windows result as not
   performed.

6. **Gates and commit.** Run the 45 Linux tests on the final tree
   (`xvfb-run -a ctest --test-dir build/linux-release --output-on-failure`) and
   `packaging/linux/verify-deb.sh` on the rebuilt `.deb`; `git status` must show only
   `CMakeLists.txt` and this `plan.md` (plus the lead's map files). Commit `CMakeLists.txt` and the
   task `plan.md` with `## Outcome`; the lead commits the map; no push.

## Done when

- `CMakeLists.txt` installs `LICENSE` at `share/doc/qtranslate/LICENSE`; the install tree, the
  Linux `tar.gz` and the `.deb` carry the file, and the AppImage's AppDir copy is confirmed or the
  AppImage build is recorded as not run with the reason.
- The artifact names and the bundle top-level layout are unchanged; the `.deb` still installs
  `copyright` plus `LICENSE`, with `GPL-3+` in the copyright.
- `verify-deb.sh` is 15/15 on the rebuilt package; the 45 Linux tests pass; no application code
  changed.
- The Windows installer/ZIP path is checked statically and recorded as not built (no MSVC).
- The coordinator commits `CMakeLists.txt` and this plan; the lead commits the map; no push.

## Risks

- **`dh_installdocs` and the extra file.** A `LICENSE` under `usr/share/doc/qtranslate` is a regular
  file next to `copyright` and `changelog.gz`; `dh_missing` with `--destdir` is a no-op, but check
  `dpkg-deb -c` and `lintian` anyway. If a Debian rule complains, move the file to
  `share/doc/qtranslate/LICENSE` only in the non-Debian install path rather than dropping it.
- **The Windows build is not available.** Never report the Windows installer as built or tested;
  the static path check is the evidence.
- **`build/license-check`** is a temporary tree; do not add it to the repository or leave it in a
  place the source package would pick up (`build/` is already ignored and excluded).
- **Release approval.** No push, tag or release from this task.

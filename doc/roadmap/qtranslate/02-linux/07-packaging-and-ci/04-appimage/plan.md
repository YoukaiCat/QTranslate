---
kind: task
status: todo
depends: [01-desktop-entry-and-install, 02-portable-tarball]
---

# AppImage

## Task

Extend `packaging/linux/package.sh` to build `qtranslate-<version>-x86_64.AppImage` with bundled
Qt, using the freedesktop AppImage tooling. The AppImage is best-effort but is built and uploaded by
CI; it is not silently dropped when the network or the tools are unavailable.

Contract:

- Download `linuxdeploy-x86_64.AppImage`, `linuxdeploy-plugin-qt-x86_64.AppImage`, and
  `appimagetool-x86_64.AppImage` into a tools directory under `build/` (for example
  `build/linux-tools/<pinned-version>/`). Pin the exact release tag/URL and a SHA256 for each tool
  in the script; verify the checksum after download and fail on a mismatch. Do not use
  `continuous` builds or an unverified download. If the tools are already present and their
  checksums match, reuse them.
- Run the tools with `--appimage-extract-and-run` (they are AppImages themselves and must not
  require FUSE). Export `ARCH=x86_64`; `linuxdeploy-plugin-qt` needs `qmake6`
  (`QMAKE=/usr/bin/qmake6` if it does not find it) and the Qt 6.8.2 installation used for the
  build.
- Build an `AppDir` from the same install staging: `usr/bin/qtranslate`,
  `usr/share/applications/qtranslate.desktop`, and the 256x256 icon. Use
  `linuxdeploy --appdir <AppDir> --desktop-file <installed desktop file> --icon-file <installed
  icon> --plugin qt --output appimage` (or run `appimagetool` explicitly after the plugin) so the
  AppDir root contains a `qtranslate.desktop` and `qtranslate.png` whose names match `Icon=` and
  `Exec=qtranslate`. Set `EXTRA_QT_PLUGINS` to include the Wayland platform and shell-integration
  plugins when `qt6-wayland` is present in the image; otherwise record that the AppImage starts
  under X11/XWayland only.
- Do not copy the tar.gz `bin/qt.conf` into the AppDir; `linuxdeploy-plugin-qt` writes the AppDir's
  own Qt configuration and libraries. Do not hand-copy plugins into the AppDir after the plugin
  ran, unless the plugin misses one and the reason is recorded.
- Name the artifact `qtranslate-<version>-x86_64.AppImage`; add its line to
  `SHA256SUMS-linux.txt` next to the tar.gz and `.deb`. Add a `--skip-appimage` option for an
  explicit local offline run; the default builds it, and CI must not pass `--skip-appimage`. If the
  download fails and no cached tool exists, the script fails with the URL and the checksum, not a
  warning.
- The AppImage is built on trixie and inherits the trixie glibc baseline (2.41); it is not expected
  to run on Astra 1.8.7 (glibc 2.36). Record that in the outcome; the Astra artifacts are child 05.
- `packaging/linux/trixie.Dockerfile`: add `file` and `curl` (or use the image's `wget`), and
  `squashfs-tools` only if `appimagetool` does not bundle `mksquashfs`. Do not add `libfuse2`; the
  `--appimage-extract-and-run` path is the supported one.

## Done when

- A trixie run produces `qtranslate-<version>-x86_64.AppImage`; `--skip-appimage` is documented and
  the default run without it fails clearly when the tool download or checksum fails.
- The tool versions, URLs, and SHA256 values are pinned in the script (or a small
  `packaging/linux/appimage-tools.txt` next to it); no `continuous` URL is used. Re-running the
  script with the tools already cached does not download again.
- `./qtranslate-<version>-x86_64.AppImage --appimage-extract-and-run --version` prints
  `QTranslate <version>` on trixie without FUSE; a GUI start under `xvfb-run -a` with a temporary
  `HOME` reaches the main window or the no-tray path and exits cleanly.
- `--appimage-extract` shows `usr/bin/qtranslate`, the bundled Qt libraries, the platform plugins
  (`qxcb` and optionally Wayland), `qtranslate.desktop`, and `qtranslate.png`; `ldd` on the
  extracted binary resolves Qt to the AppDir, not to `/usr/lib`; the `.desktop` in the AppDir has
  `Exec=qtranslate`/`Icon=qtranslate`.
- `SHA256SUMS-linux.txt` contains the AppImage line and `sha256sum -c` passes; the archive/deb
  lines from earlier children remain.
- Debian 13 still builds and tests 28/28; Astra is not used for the AppImage run and is recorded as
  not applicable (glibc baseline); Windows is recorded as not performed; no documentation edited.

## Risks

- `linuxdeploy` excludes some libraries by default and may leave a needed ICU/pcre2/glib library out
  of the AppDir. Check `ldd` on the extracted binary and on `usr/lib/libQt6Gui.so.6`; if a library
  is missing, add it explicitly with `--library` or an exclusion override and record why.
- `linuxdeploy-plugin-qt` needs a working `qmake6`/Qt installation and may pick the host's Qt if
  `QMAKE` is wrong. Use the same Qt 6.8.2 as the build; check the bundled `libQt6Core` version in
  the AppDir.
- The AppImage tooling is downloaded, not packaged. Pin versions and SHA256; a network-less local
  run can use `--skip-appimage`, but CI must fail rather than omit the artifact. A checksum
  mismatch is a hard failure.
- FUSE is unavailable in containers; always use `--appimage-extract-and-run` for the tools and for
  the produced AppImage test. Do not add `libfuse2` as a build dependency just to run the tools.
- `appimagetool` may need `mksquashfs`; if the bundled one is absent, install `squashfs-tools` in
  the image. Do not rely on the host's `mksquashfs` outside the container.
- The AppDir's `qt.conf`/plugin layout belongs to `linuxdeploy-plugin-qt`; do not overwrite it with
  the portable archive's `qt.conf`. The two layouts are different and both must work.
- `libsecret`/glib may or may not be bundled by `linuxdeploy`; record which ones are inside the
  AppDir and which are expected on the target. Do not claim the AppImage is fully self-contained
  beyond its glibc baseline.
- The AppImage is trixie-built and requires glibc 2.41; it does not run on Astra. Do not use the
  AppImage as the Astra fallback; child 05 builds the Astra artifacts natively.
- The artifact name must not collide with the Windows artifacts or the tar.gz/deb names; the
  release job downloads all artifact sets into one directory.
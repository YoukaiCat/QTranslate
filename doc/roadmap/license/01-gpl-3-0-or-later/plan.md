---
kind: stage
status: in-progress
base: 17dd709
---

# GPL-3.0-or-later

## Goal

License the project under the GNU General Public License version 3 or later, record it in the
package metadata and the documentation, and ship the license text with the binary artifacts. The
choice was checked against the current and planned dependencies; see the root `## Done when`.

## What to do, what is touched

- Add `LICENSE` with the verbatim GPL-3 text.
- `debian/copyright`: replace the all-rights-reserved DEP-5 stanza with `License: GPL-3+` and the
  standard notice; the file is installed as `/usr/share/doc/qtranslate/copyright`.
- `README.md`/`README.ru.md`: a `## License` section, the GPL-3.0-or-later notice and a link to
  `LICENSE`; keep the two READMEs in sync.
- `AGENTS.md`, `doc/architecture.md`, `doc/handoff.md`, `debian/README.Debian`: state the license
  and point at `LICENSE`; remove the "no license file" wording from the current-state text.
- `CMakeLists.txt`: install `LICENSE` to `share/doc/qtranslate` so the Windows `dist` (installer
  and ZIP), the Linux portable bundle and the AppImage carry it; the `.deb` gets it under
  `/usr/share/doc/qtranslate/`.
- A one-line cross-reference in `doc/roadmap/debian/journal.md`: the closed map's
  all-rights-reserved statements are historical and are superseded by this map.
- Application code, tests and the packaging logic do not change otherwise.

## Children

1. [01-license-docs-and-metadata](01-license-docs-and-metadata/plan.md) — the license text, the
   package metadata and all live documents. Documentation only; the lead executes it and commits it
   as a docs commit.
2. [02-license-in-bundles](02-license-in-bundles/plan.md) — install `LICENSE` in the CMake install
   tree, verify the Windows/Linux bundle paths and the `.deb` copyright, and commit the build
   change. `depends: [01-license-docs-and-metadata]`.

## Done when

- `LICENSE` holds the GPL-3 text; `debian/copyright` is DEP-5 with `GPL-3+` and no "all rights
  reserved" grant in the live tree; the READMEs, `AGENTS.md`, `doc/architecture.md`,
  `doc/handoff.md` and `debian/README.Debian` describe the license consistently.
- `CMakeLists.txt` installs `LICENSE` under `share/doc/qtranslate`; the Linux tar.gz and the
  AppImage/Windows bundle carry it, the `.deb` carries `LICENSE.gz` (`dh_compress`) next to the
  GPL `copyright`, the Windows `dist`/ZIP/installer path is checked statically (no MSVC here), and
  the package gate and the 45 Linux tests still pass.
- The native `.deb` gate is unchanged; `lintian` on the source package does not complain about the
  copyright; the `debian` map's closed outcome is cross-referenced in its journal, not rewritten.
- No application code, tests or behavior change; no push, tag or release.

## Risks and what is needed from the user

- **Version choice.** The user said "GPL"; GPLv2-only is incompatible with Qt 6 and OpenSSL, so the
  stage uses GPL-3.0-or-later. If the user wanted a different GPL version, this must be reopened
  before the license is announced.
- **Data licenses.** The planned CC-BY-NC model weights are not shipped by the project; if they are
  ever distributed, they need a separate check and are not covered by this stage.
- **Windows.** The Windows build, installer and ZIP cannot be built here (no MSVC); the install
  path is verified statically and the Windows result is recorded as not performed.
- **Release approval.** Pushing, tagging and publishing remain the user's decisions.

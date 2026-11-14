---
kind: root
status: in-progress
lang: en
source: doc/architecture.md
branch: main
base: 17dd709
---

# GPL-3.0-or-later license — roadmap

QTranslate has no license file and its `debian/copyright` states all rights reserved. The user
asked for a GPL license if it conflicts neither with the current dependencies nor with the planned
work.

**GPL-3.0-or-later, not GPLv2.** Qt 6 is LGPLv3/GPLv3 (GPLv2-only is incompatible with it), and
OpenSSL is Apache-2.0 (also incompatible with GPLv2-only). The other dependencies are compatible
with GPLv3: libsecret is LGPL-2.1+, X11/Xtst are MIT, libstdc++ has the runtime exception, and the
system libraries are not linked into a combined work. Planned work is compatible too: Argos
Translate, CTranslate2, llama.cpp and ONNX Runtime are MIT, LanguageTool is used over HTTP
(LGPL-2.1), and the NLLB/Seamless CC-BY-NC model weights are data the project does not ship; if
they are ever distributed, that is a separate data-licensing check, not a GPL conflict. GPLv3 is
OSI-approved, which is also a prerequisite for SignPath Foundation code signing.

The work adds the license text, updates the metadata and documents, and ships the text with the
binary bundles. Application code does not change.

## Done when

- `LICENSE` contains the GNU GPL version 3 text, and `debian/copyright` is DEP-5 with
  `License: GPL-3+` and the standard notice (no "all rights reserved" left in the live package
  metadata).
- `README.md`, `README.ru.md`, `AGENTS.md`, `doc/architecture.md`, `doc/handoff.md` and
  `debian/README.Debian` state GPL-3.0-or-later and point at `LICENSE`; both READMEs stay in sync.
- The native `.deb` installs the GPL copyright, and the Windows ZIP/installer and the Linux
  tar.gz/AppImage carry `LICENSE` from the CMake install tree; no application code changes.
- The native build and the Linux 45/45 tests still pass; the package gate is unchanged.
- The `debian` map stays closed: its "all rights reserved" statements are historical and the new
  `license` map records the change; no `qtranslate` node is changed.
- No push, tag or release without the user's explicit approval.

## Stages

1. [GPL-3.0-or-later](01-gpl-3-0-or-later/plan.md) — license files, metadata, documentation and
   bundle packaging.

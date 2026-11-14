# GPL license roadmap journal

## Where we left off

The map is closed: the stage and root are `done`, the root `## Outcome` is in the closing map
commit, and `doc/roadmap/README.md` lists `license` as `done`. The accepted tree is
`17dd709..5a48732`; the closing commit carries this journal, the stage outcome and the root
outcome. There is no active work on this map. The `qtranslate` map is untouched and remains
`in-progress` at `06-llm-actions`, so the next session continues there. Nothing was pushed. Branch
`main`.

## Journal

- 2026-11-14 — The user asked for a GPL license if it conflicts neither with the current
  dependencies nor with the planned work. Decision: **GPL-3.0-or-later**. GPLv2-only was rejected:
  Qt 6 is LGPLv3/GPLv3 and OpenSSL is Apache-2.0, both incompatible with GPLv2-only. The current
  dependencies (libsecret LGPL-2.1+, X11/Xtst MIT, libstdc++ runtime exception) and the planned
  ones (Argos/CTranslate2/llama.cpp/ONNX Runtime MIT, LanguageTool over HTTP) are GPLv3-compatible;
  the CC-BY-NC NLLB/Seamless model weights are data and not shipped, so they do not make the code
  license conflict. GPLv3 is OSI-approved, so it also satisfies the SignPath Foundation license
  prerequisite if code signing is pursued later. The closed `debian` map keeps its historical
  all-rights-reserved outcome; this map records the license change and the `debian` journal gets a
  one-line cross-reference.

- 2026-11-14 — Stage `01-gpl-3-0-or-later` accepted and the map closed. Documentation and metadata
  landed in `9044d7c`; the CMake install rule and the `LICENSE.gz` gate allowlist in `15df6a3`;
  the package/bundle descriptions were updated in `1cc4574`; the acceptance fixed the stale
  `package.sh` comment and admitted `README.Debian.gz` in `5a48732`. The acceptance re-checked
  `LICENSE` (identical to the system GPL-3 text), `debian/copyright` (`GPL-3+`), the install tree,
  the Astra tar.gz and `.deb`, the gate (15/15, self-test 4/4), the 45 Linux tests and `lintian`
  (no copyright/license error); Windows and the Astra AppImage were recorded as not performed.
  No push, tag or release.

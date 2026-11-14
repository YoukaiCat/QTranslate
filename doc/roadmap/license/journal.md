# GPL license roadmap journal

## Where we left off

Current node: stage `01-gpl-3-0-or-later` is `in-progress` at `17dd709`. Task
[01-license-docs-and-metadata](01-gpl-3-0-or-later/01-license-docs-and-metadata/plan.md) is `done`
(`9044d7c`); task [02-license-in-bundles](01-gpl-3-0-or-later/02-license-in-bundles/plan.md) is
`done` (`15df6a3`): `CMakeLists.txt` installs `LICENSE` at `share/doc/qtranslate`, the gate admits
`LICENSE.gz` in the `.deb`, the Astra tar.gz carries `LICENSE`, and the docs were updated in
`1cc4574`. The stage acceptance is next; after it the root outcome and `status: done` in this map
and `doc/roadmap/README.md`. Branch `main`. Nothing is pushed.

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

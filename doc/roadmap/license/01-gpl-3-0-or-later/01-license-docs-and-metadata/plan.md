---
kind: task
status: done
detailed: true
---

# License text, package metadata and documents

## Task

Add the license text and make every live document and the Debian package metadata state the same
terms: GNU GPL version 3 or later. This is a documentation and metadata task; the lead executes it
without a coordinator and commits it as a documentation commit.

The license choice is GPL-3.0-or-later: GPLv2-only is incompatible with Qt 6 (LGPLv3/GPLv3) and
OpenSSL (Apache-2.0). The compatibility analysis is in the root `## Done when` and the journal.

## Steps

1. **`LICENSE`.** Put the verbatim GNU GPL version 3 text at the repository root: copy
   `/usr/share/common-licenses/GPL-3` (674 lines, starts with `GNU GENERAL PUBLIC LICENSE`,
   `Version 3, 29 June 2007`) to `LICENSE`, mode 0644. Do not add a project-specific preamble; the
   standard text is the license.

2. **`debian/copyright`.** Replace the all-rights-reserved stanza with DEP-5 and `GPL-3+`:

   ```
   Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
   Upstream-Name: QTranslate
   Source: https://github.com/youkaicat/QTranslate

   Files: *
   Copyright: 2026 Vladislav Mileshkin
   License: GPL-3+
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    .
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    .
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
    .
    On Debian systems, the complete text of the GNU General Public
    License version 3 can be found in "/usr/share/common-licenses/GPL-3".
   ```

   The `Comment` about the absent license is removed; the file is installed as
   `/usr/share/doc/qtranslate/copyright`.

3. **`README.md` and `README.ru.md`.** Add a `## License` / `## Лицензия` section at the end (after
   the privacy paragraph) with the standard GPL-3.0-or-later notice and a link to `LICENSE`:

   ```markdown
   ## License

   QTranslate is free software: you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version. See [`LICENSE`](LICENSE) for the full text.
   ```

   The Russian README gets the same facts in Russian and the same `LICENSE` link. Keep the section
   after "Known limitations"/"Известные ограничения" and the privacy note; do not reorder unrelated
   sections.

4. **`AGENTS.md`.** Add one bullet to `## Project` after the design-docs bullet: the project is
   GPL-3.0-or-later, `LICENSE` is the source of truth, and `debian/copyright` and both READMEs must
   stay in sync. Keep it imperative; do not add a Jira key.

5. **`doc/architecture.md`.** Add one sentence to the introduction (after the reference-document
   paragraph) that the project is licensed under GPL-3.0-or-later and `LICENSE` holds the text. Do
   not touch the version, packaging or functional sections.

6. **`doc/handoff.md`.** Update the "Native Debian packaging (2026-11-14)" bullet that says
   `copyright` is "DEP-5, all rights reserved — the project has no license file and none was
   added" to "DEP-5, GPL-3.0-or-later; `LICENSE` holds the text". Add a short license line to the
   same section or the `## Git`/current-state area: license chosen on 2026-11-14, GPLv2-only
   rejected because of Qt 6/OpenSSL. Do not rewrite the historical release sections.

7. **`debian/README.Debian`.** Add a `License` section at the end: GPL-3.0-or-later, the upstream
   `LICENSE`, and the installed `/usr/share/doc/qtranslate/copyright`. Do not change the build
   command or the source-package text.

8. **`doc/roadmap/debian/journal.md`.** Append one journal entry: the license question was settled
   after that map closed (GPL-3.0-or-later), the all-rights-reserved statements in its outcome and
   stage plans are historical, and the new `license` map is the current record. Do not edit the
   closed outcomes or stages.

9. **Gates.** From the repository root:

   ```bash
   test -f LICENSE && head -2 LICENSE
   grep -n 'License: GPL-3+' debian/copyright
   grep -n 'all rights reserved\|All-Rights-Reserved' debian/copyright README.md README.ru.md \
       AGENTS.md doc/architecture.md doc/handoff.md debian/README.Debian
   # only historical statements in doc/handoff.md / the debian map, if any; none in debian/copyright
   grep -c 'GPL-3.0-or-later' README.md README.ru.md AGENTS.md doc/architecture.md doc/handoff.md \
       debian/README.Debian
   grep -n '](LICENSE)' README.md README.ru.md
   ```

   Every relative link must resolve; the READMEs must state the same version and the same link.

10. **Commit.** The lead commits `LICENSE` and the documentation/metadata files as one commit (the
    map files travel with the map commit). No push.

## Done when

- `LICENSE` is the verbatim GPL-3 text and `debian/copyright` is DEP-5 with `GPL-3+` and no
  all-rights-reserved grant.
- `README.md`, `README.ru.md`, `AGENTS.md`, `doc/architecture.md`, `doc/handoff.md` and
  `debian/README.Debian` state GPL-3.0-or-later, both READMEs link to `LICENSE`, and every relative
  link resolves.
- The closed `debian` map is cross-referenced from its journal without rewriting its outcomes.
- No application code, tests, `CMakeLists.txt` or packaging script changes in this task; the
  bundle install is task 02.
- The lead committed the docs; no push.

## Risks

- **The GPL text is long.** Do not hand-edit it; copy the system copy verbatim and only check the
  first lines.
- **The `debian` map is closed.** Its all-rights-reserved outcome is historical; the journal note
  is the bridge, not a rewrite of the closed nodes.
- **The READMEs are a translation pair.** A fact added to one must be added to the other in the
  same commit.

## Outcome

Done by the lead as a documentation and metadata task; committed as `9044d7c` (eight files, no
code).

- `LICENSE` is the verbatim GPL-3 text (674 lines, copied from `/usr/share/common-licenses/GPL-3`).
- `debian/copyright` is DEP-5 with `License: GPL-3+` and the standard GPLv3 notice; the
  all-rights-reserved stanza and its comment are gone.
- `README.md` and `README.ru.md` have the License / Лицензия section naming GPL-3.0-or-later and
  linking `LICENSE`; `AGENTS.md` names the license and the sync rule; `doc/architecture.md` states
  it in the introduction and in the `debian/` metadata descriptions; `doc/handoff.md` records the
  choice and the GPLv2 rejection; `debian/README.Debian` has the License section pointing at the
  upstream `LICENSE` and the installed `copyright`.
- `doc/roadmap/debian/journal.md` now notes that the closed map's all-rights-reserved statements
  are historical and the `license` map is the current record.

Verification: no `all rights reserved`, `All-Rights-Reserved` or "no license file" remains in the
live documents or `debian/copyright`; `GPL-3.0-or-later` appears in both READMEs, `AGENTS.md`, the
architecture, the handoff and `debian/README.Debian`; every relative link in the touched documents
resolves; `LICENSE` starts with the GPL-3 header. The `debian` map nodes were not rewritten.

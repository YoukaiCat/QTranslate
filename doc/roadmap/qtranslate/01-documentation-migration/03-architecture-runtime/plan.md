---
kind: task
status: done
detailed: true
depends: [02-architecture-core]
---

# Translate architecture sections 4.8-10

## Task

Translate the rest of `doc/architecture.md`: modules 4.8-4.16, scenarios 5, errors 6, logging/security 7, testing 8, decisions 9, and implementation order 10. Section 11 stays unchanged. Update future-work references to the new roadmap numbering, keep old plan numbers in parentheses, and make section 10 point to the moved archive.

## Steps

1. Translate 4.8-4.16 (currently lines 747-1261): 4.8 languages, 4.9 `TranslationManager`, 4.10 `NetworkManager`, 4.11 `AppSettings`/`SettingsStore`, 4.12 `Autostart`, 4.13 UI (`TrayIcon`, `PopupWindow`, `MainWindow`, `LanguageComboBox`, `SettingsDialog`, `AppIcon`, localization), 4.14 portability/Linux, 4.15 proofreading plan, 4.16 LLM actions/dictionary. Preserve settings keys (`hotkeys/pauseInFullscreen`, etc.), class names, `enum` values, INI sections, log strings, Qt identifiers, and test names.
2. Translate sections 5-10 (currently lines 1262-1635): 5.1-5.6 scenarios, 6 error handling, 7 logging/security/privacy, 8 testing including 8.1-8.4 release checks, 9.1 decisions table, 9.2 limitations and possible development, 10 implementation order.
3. Stage numbering:
   - completed old stages become “old plan stage N” (for example localization was old stage 12; proofreading is roadmap stage 5 (old stage 15); LLM actions are roadmap stage 6 (old stage 16); the dictionary is roadmap stage 7 (old stage 17));
   - Linux is “roadmap stage 2 (old stage 11)”;
   - free web endpoints are “roadmap stage 3 (old stage 13)”;
   - local offline translation is “roadmap stage 4 (old stage 14)”.
   - Update all occurrences at current lines 775, 802, 906, 1058, 1067, 1099, 1115, 1132, 1138, 1180, 1576, 1593, 1607, and 1609; re-grep after the edits.
4. Section 9.2: translate the limitation list; it is the source for the known gaps added to `doc/handoff.md` in task 04. Keep links to the design notes.
5. Section 10: translate and state that the old flat plan was moved to `doc/roadmap/qtranslate/legacy-plan.md` in stage 1 and that the active plan is `doc/roadmap/qtranslate/plan.md`. The current text is future tense; make it present or past.
6. Keep quoted Russian UI labels only where the label itself matters; otherwise translate them. List the kept labels.
7. Do not touch section 11 and do not translate source-code comments outside the document.
8. Check that links in the changed range resolve. Do not commit.

## Done when

- `doc/architecture.md` is fully English from the title through section 10; section 11 is unchanged.
- Stage references use the new roadmap numbers with old numbers in parentheses for future work and “old plan stage N” for completed old stages; no ambiguous bare old stage numbers remain.
- Section 10 points to the active roadmap and says the archive was moved in stage 1.
- Technical identifiers, settings keys, API names, test names, tables, and numbers are unchanged.
- No Cyrillic remains except quoted Russian UI labels and native language names (list them).
- All relative links resolve; only `doc/architecture.md` changed; no build required.

## Risks

- The file is long; work top-down and keep a heading checklist to avoid skipping subsections.
- Old and new stage numbers are easy to confuse; record the mapping in the outcome: new 2/3/4/5/6/7 ↔ old 11/13/14/15/16/17.
- The decisions table is dense; do not change decisions, only language.
- Section 11 is the migration policy; leave it untouched even though its “currently Russian” context describes the pre-migration state.

## Outcome

Translated the rest of `doc/architecture.md`: modules 4.8-4.16, scenarios 5, error handling 6, logging/security/privacy 7, testing 8, decisions 9, and implementation order 10. Section 11 is untouched and byte-identical to `HEAD`. The document is now English from the title through section 10; only three legitimate Russian strings remain. The interrupted sessions' work was verified and finished, not redone.

- Stage references: future work uses the new roadmap numbering with old numbers in parentheses — roadmap stages 2/3/4/5/6/7 correspond to old stages 11/13/14/15/16/17; completed old stages are written `old plan stage N` (localization is `old plan stage 12`). Section 10 states that the old flat plan was moved to `doc/roadmap/qtranslate/legacy-plan.md` in stage 1 of the migration and points to the active roadmap `roadmap/qtranslate/plan.md`.
- Kept Russian strings: `Русский` in the §4.13 UI-language list (native language name), `English ↔ Русский` in §8.1 (native language name in the UI), and `Привет, мир` in §8.1 (the actual translated output of the smoke test). No other Cyrillic remains.
- Verification: `grep -nP '[А-Яа-яЁё]' doc/architecture.md` finds exactly those three strings; the tail from `## 11. Documentation and roadmap` is identical to `git show HEAD:doc/architecture.md`; in the translated sections, `grep -nP '\bstage\s+\d+' doc/architecture.md` shows only `roadmap stage 2 (old stage 11)`, `old plan stage N`, and the migration's `stage 1`, while the untouched section 11 keeps its original references; backticked spans, headings, table rows, list items, code fences, and digit tokens match `HEAD` except for translated prose, decimal separators, and the requested link changes in section 10; all 13 relative Markdown links resolve from `doc/`; `git diff --check` is clean; only `doc/architecture.md`, this task's `plan.md`, and `doc/roadmap/qtranslate/journal.md` are modified. No build was required.
- Found but not fixed, out of scope: the 3.1 tree misses `src/ai/openaichat.*`, `src/core/uilanguages`, the DeepL web and OpenAI-compatible services, the `autostart_stub.cpp`/`secretprotector_stub.cpp` stubs, and the newer tests; 3.2 misses only `tst_translations` and `tst_uilanguages`; §11.1 still calls `doc/plan.md` active; the comment at `src/platform/selectiongrabber_win.cpp:248` still points to the removed `doc/plan.md`.

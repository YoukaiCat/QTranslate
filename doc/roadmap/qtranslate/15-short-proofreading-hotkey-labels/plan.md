---
kind: task
status: done
detailed: true
depends: [09-proofreading-standalone]
---

# Short labels for the proofreading hotkeys

## Task

User report (2026-11-16): on the **Hotkeys** page the translation rows are short ("Translate in a
popup:", "Replace the selected text with the translation:") while the two checking rows spell out the
whole scenario ("Check the selection in another application and show the result in a popup:", "Check
the selection in another application and replace it with the corrected text:"). The page reads
inconsistently: the section is about global hotkeys, the "another application" part is what a global
hotkey means, and the long labels wrap in the form.

Replace them with the short, symmetric pair the user asked for:

- `Check in a popup:` (currently "Check the selection in another application and show the result in a
  popup:");
- `Replace with the corrected text:` (currently "Check the selection in another application and
  replace it with the corrected text:").

The behaviour is unchanged: the first opens the checking popup for the selection, the second checks
and replaces the selection immediately. The page already carries the hint "Global hotkeys are
disabled while a key combination field is focused. Leave a field empty to disable that action.", so
the cut sentence adds nothing.

## Steps

1. In `src/ui/settingsdialog.cpp` (the Hotkeys page, around lines 131-135) replace the two `tr()`
   sources with exactly `Check in a popup:` and `Replace with the corrected text:` — the English text
   is the translation source, so the wording must match character for character. Do not touch the
   translation rows, the hint, the object names (`proofreadingHotkey`, `proofreadReplaceHotkey`) or
   `values()`/`loadValues()`.
2. Regenerate the catalogues with the project target (`update_translations`, which runs `lupdate`
   without `-no-obsolete`), then remove the two obsolete messages so the files keep 0 vanished
   entries, and translate both new sources in every one of the 14 `i18n/qtranslate_*.ts` files:
   - ru: `Проверить во всплывающем окне:` and `Заменить исправленным текстом:` (the user corrected the
     first from "Проверять" to "Проверить", matching the imperative of the second);
   - the other twelve: follow each file's existing style for the neighbouring hotkey rows
     (`Translate in a popup:`, `Replace the selected text with the translation:`), keeping the colon
     and the imperative/verb form that file already uses.
   Add the two new messages with 0 unfinished and 0 vanished; the total stays 383 per file.
3. Run `lrelease` through the same target so the embedded `.qm` matches the sources; do not commit
   `build/` output.
4. Check that nothing else referenced the old sources: `grep -rn "Check the selection in another
   application" src/ tests/ i18n/ doc/` must find nothing outside the archive and this map.

## Done when

- `src/ui/settingsdialog.cpp` contains the two new `tr()` sources and no other label change; the
  dialog works as before (same object names, same `QKeySequenceEdit`s, empty still means disabled).
- All 14 `.ts` files have exactly 383 messages, 0 unfinished and 0 vanished, with `ru` translated as
  above and the other twelve consistent with their neighbouring hotkey rows; `lrelease` succeeds.
- `tst_settingsdialog` and `tst_translations` pass without edits; all 53 Linux tests pass, Debug and
  Release build without warnings on the host and in `qtranslate-trixie`.
- No code change beyond the two strings, no new dependency, no documentation edit outside
  `doc/roadmap/`.

## Risks

- A `.ts` edit that drops the colon or the leading verb form makes one language read differently from
  its neighbours; the review checks the pair next to the translation rows.
- `lupdate` renumbers `<location>` lines of the whole file: the diff stays large, and the review only
  looks at the message blocks.

## Outcome

Accepted on 2026-11-16.

- `src/ui/settingsdialog.cpp:130-131` — the two Hotkeys rows now read `Check in a popup:` and
  `Replace with the corrected text:`, one `addRow` each. Nothing else changed: the translation rows,
  the hint, `proofreadingHotkey`/`proofreadReplaceHotkey`, `loadValues()`, `values()` and the `validate()`
  key list are untouched, and an empty field still disables the action.
- All 14 `i18n/qtranslate_*.ts` were regenerated with the `update_translations` target (Qt 6.10.2
  `lupdate`; the Qt 5.15 `/usr/bin` binaries were not used), the two obsolete blocks were deleted and
  the two new sources translated. Every file has 383 messages, 0 unfinished, 0 vanished and 0 obsolete;
  `release_translations` reported 383 finished per file.
- `ru` is exactly `Проверить во всплывающем окне:` and `Заменить исправленным текстом:` as the user
  corrected it. The other twelve follow their own file's convention for the neighbouring hotkey rows:
  infinitive in `de`, `es`, `fr` and `pt_BR`; imperative in `ar`, `hi`, `it`, `pl`, `tr` and `uk`; the
  polite `ます` sentence form in `ja` and the nominal form with a colon in `ko`; a full-width colon in
  `zh_CN` and a non-breaking space before the colon in `fr`. `ja` and `ko` deliberately keep their own
  punctuation instead of an ASCII colon, because that is what the surrounding rows use.
- Review accepted one remark and had it fixed: `hi` said `सुधारे गए पाठ` for "corrected text" while the
  other seven messages of the file say `सुधरा हुआ पाठ`. Rejected as file convention or generator output
  rather than a defect: the `ja`/`ko` punctuation and the `uk` imperative above, and the apostrophe
  normalisation below.
- Side effect of `lupdate` 6.10, kept: `i18n/qtranslate_fr.ts` now escapes the apostrophe in
  `choisissez d&apos;autres` like the 40 other `&apos;` in that file. A manual revert would be undone by
  the next `update_translations`.
- Step 4 is clean in `src/`, `tests/` and `i18n/`. The old wording survives as prose, not as a label, in
  `doc/proofreading.md:33` and `doc/releases/0.13.0.md:36`, and as quoted history in the stage-5 map and
  in this plan; `Done when` forbids documentation edits outside `doc/roadmap/`, so they were left alone.
- Verification: the host Debug and Release builds are warning-free and pass 53/53 tests each,
  `qtranslate-trixie` Release builds and passes 53/53, and `tst_settingsdialog` and `tst_translations`
  pass without edits. Windows was not built (no MSVC on this machine): reviewed by diff.
- Not fixed, recorded for later: `i18n/README.md:42` tells the translator to run `lupdate` with
  `--no-obsolete` while the `update_translations` target in `CMakeLists.txt` does not pass it, which is
  why the obsolete blocks have to be deleted by hand; the portal description of the same action in
  `src/platform/hotkeymanager_linux.cpp:82-85` still spells out the scenario but is a separate string in
  the desktop's shortcut UI; and `SettingsDialog::validate()` fills a `name` field that is never read, so
  its four colon-less `tr()` sources are dead. The last two predate this task.

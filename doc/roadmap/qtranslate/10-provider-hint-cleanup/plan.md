---
kind: task
status: done
detailed: true
---

# Remove the provider privacy sentences from the settings hints

## Task

The settings hints repeat that translated text leaves the machine: "The text is sent to a third
party" (Yandex, Bing, MyMemory, Reverso and the fallback row), "and the text is sent to DeepL"
(DeepL free web), "The text is sent to the selected provider" (OpenAI-compatible) and "The text is
sent to the selected instance and engine" (Mozhi).

User decision (2026-11-15): remove these sentences everywhere in the settings. Translation is
remote by nature, and the per-provider wording is inconsistent: Google has no such sentence at all,
DeepL has one only in free-web mode, and the different phrasings read as if only some providers send
text away. Keep the provider-specific warnings: unofficial endpoint, no key, rate limits, blocks,
long text in several requests, MyMemory's missing detection, Reverso's six languages and missing
detection, the fallback failure list and `None`.

Keep the single global statement in `README.md:315-317` and `README.ru.md:320` ("the text is sent to
the selected provider; a local server keeps it on your machine"), and keep the LanguageTool hint on
the Text checking page, which explains the public versus self-hosted checking endpoint.

The user rejected a shared note at the top of the Services page, repeating the sentence in every
provider group, and leaving the hints as they are.

## Steps

1. **`src/ui/settingsdialog.cpp`: rewrite the eight hints in `buildUi()`.** Replace only the `tr()`
   argument of each label. Keep the object name, `setWordWrap(true)`, the form rows and the adds
   untouched, and change no other line of the file. The texts stay one `tr()` with implicit literal
   concatenation; re-wrap each to the surrounding ~100-column style. Cut the removed sentence
   together with the space in front of it: no double space, no trailing space before the closing
   quote, no `, and` left dangling. The exact final texts:

   - `m_fallbackHint` (currently `:186-190`):
     `When the selected service fails with no connection, a timeout, a rate limit, an exceeded quota or an invalid key, the text is sent once more to the fallback service. None turns it off.`
   - `yandexHint` (`:249-253`):
     `Yandex web is an unofficial endpoint and needs no key. It may stop working, apply rate limits or block requests. Long text is sent in several requests.`
   - `bingHint` (`:262-266`):
     `Bing web is an unofficial endpoint and needs no key. It may stop working, apply rate limits or block requests. Long text is sent in several requests.`
   - `m_deepLWebHint` (`:293-296`):
     `Available without an API key. This unofficial endpoint may stop working, apply rate limits or block requests.`
     The removed clause is joined by `, and`, so the comma after `block requests` becomes the final
     full stop: the sentence ends `block requests.`, with no `,` and no second sentence.
   - `llmHint` (`:345-350`):
     `Uses the OpenAI-compatible Chat Completions API. The default server is OpenRouter; the URL can point to another compatible endpoint such as OpenAI, Ollama, llama.cpp or TeamRouter. The model must be available to your key.`
   - `myMemoryHint` (`:366-370`):
     `MyMemory is a free translation API and needs no key. It cannot detect the source language, and long text is sent in several requests.`
   - `m_mozhiHint` (`:413-418`):
     `Mozhi is a proxy to public translation engines. Instances are unofficial: they may stop working or rate-limit requests, and some engines may be disabled on an instance. A self-hosted instance's terms and stability are your responsibility. Long text is sent in several requests.`
   - `reversoHint` (`:429-434`):
     `Reverso is an unofficial endpoint and needs no key. It supports only English, French, German, Spanish, Italian and Russian and cannot detect the source language. It may stop working, apply rate limits or block requests. Long text is sent in several requests.`

   After this step `rg -n "third party|sent to DeepL|selected provider|selected instance and engine" src`
   must return only `src/services/mozhiservice.h:11`, the internal comment that a no-key service
   still sends text to a third party. Keep it: it describes the service, not the UI.

   The labels only become shorter; they still word-wrap and the Services page still scrolls. Do not
   touch sizes, form rows, group boxes, object names or connects.

2. **`tests/tst_settingsdialog.cpp`: assert the sentences are gone and the rest is intact.** Invert
   the positive checks, keep every other substring assertion of the same test, and do not compare
   whole hint strings:

   - `myMemoryGroup()`: `:465` becomes
     `QVERIFY(!hint->text().contains(QStringLiteral("third party")));`; keep `no key` (`:463`) and
     `source language` (`:464`) and add
     `QVERIFY(hint->text().contains(QStringLiteral("several requests")));` so the retained
     "long text in several requests" sentence stays covered (this hint has no such check today).
   - `mozhiGroup()`: `:506` becomes
     `QVERIFY(!hint->text().contains(QStringLiteral("selected instance")));`. The phrase only ever
     appeared in the removed sentence; `Instances are unofficial`, `on an instance` and
     `A self-hosted instance's` keep matching. Keep `unofficial`, `rate-limit`, `self-hosted` and
     `several requests` (`:502-505`).
   - `yandexGroup()` `:607`, `bingGroup()` `:631`, `reversoGroup()` `:655` and
     `fallbackRowOnTranslationPage()` `:725`: the same inversion to
     `QVERIFY(!hint->text().contains(QStringLiteral("third party")));`. Keep the failure list, `None`,
     `unofficial`, `no key`, `rate`, `block`, `several requests`, `English` and `source language`
     assertions as they are.
   - `deepLWebHint()`: add
     `QVERIFY(!hint->text().contains(QStringLiteral("sent to DeepL")));` next to the existing
     `without an API key` and `unofficial` checks (`:835-838`).
   - Add a slot `llmHint()` right after `deepLWebHint()` (they are inline under `private slots:`,
     `:79`): find `llmHint`, `QVERIFY` it exists and `wordWrap()`, contains `OpenAI-compatible`,
     `OpenRouter` and `your key`, and
     `QVERIFY(!hint->text().contains(QStringLiteral("selected provider")));`. This hint has no text
     test today.
   - Leave the group/order/`freeWebGateIsGone()`/`providerOrder()` tests alone: no widget, object
     name or ordering changes.

3. **Regenerate the `.ts` sources after the code edit.** First copy the 14 `i18n/qtranslate_*.ts` to
   `/tmp/ts-before/` (step 5 compares against that copy), then from the repository root:

   `cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release`
   `cmake --build build/linux-release --target update_translations`

   lupdate scans the `qtranslate` target (plus `src/platform/hotkeymanager_win.cpp`, see
   `CMakeLists.txt:186-198`), so it produces the `<source>` values from the code — never retype a
   `<source>` by hand. The existing `build/linux-release` reports 45 tests today while `AGENTS.md`
   and a fresh configure expect 46, so reconfigure it (or use a new build directory) before trusting
   the test count.

   Expect in each of the 14 `i18n/qtranslate_*.ts`: the eight `<source>` elements now carry the new
   texts, the eight `<translation>` elements keep the old translations with `type="unfinished"`,
   still 374 `<message>` entries, no `type="vanished"`, and `<location>` churn (the C++ file loses
   about eight lines, so later locations shift up). If anything shows up as `type="vanished"`, rerun
   `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json` and
   check the count again; if an unexpected message changed, stop and report it instead of accepting
   it.

4. **Remove the sentence from the 14 translations.** For each of the eight messages in each of the
   14 files, delete the final sentence of the `<translation>` and remove `type="unfinished"` from
   that element. General rule: delete the sentence together with its single separating space; in
   `zh_CN` and `ja` there is no separating space, so the sentence starts right after the previous
   `。`; in `hi` the separator is a space and the sentence ends with `।`. Leave every other sentence,
   every spelling and the order untouched. If a language joined the removed clause to the previous
   sentence with a separator instead of starting a new sentence, delete the separator too; DeepL is
   the known case below, and the tail printout of step 5 shows any other.

   `ru_RU` reference tails (each deleted together with the single space in front of it):
   `Текст отправляется третьей стороне.` for the fallback row, MyMemory, Yandex, Bing and Reverso;
   `Текст отправляется выбранному провайдеру.` for the OpenAI-compatible hint;
   `Текст отправляется выбранному экземпляру и движку.` for Mozhi.

   The DeepL hint is the known case: its removed clause is joined to the previous sentence instead of
   standing alone. Delete the joiner together with the clause and keep the final full stop of the
   retained sentence:

   - `qtranslate_ar.ts`: delete `؛ يُرسَل النص إلى DeepL`
   - `qtranslate_de.ts`: delete `; der Text wird an DeepL gesendet`
   - `qtranslate_es.ts`: delete `; el texto se envía a DeepL`
   - `qtranslate_fr.ts`: delete ` ; le texte est envoyé à DeepL` (the space before `;` goes too)
   - `qtranslate_hi.ts`: delete `; पाठ DeepL को भेजा जाता है`
   - `qtranslate_it.ts`: delete `; il testo viene inviato a DeepL`
   - `qtranslate_ja.ts`: delete the whole sentence `テキストは DeepL に送信されます。`
   - `qtranslate_ko.ts`: replace `차단할 수 있으며, 텍스트는 DeepL로 전송됩니다.` with
     `차단할 수 있습니다.` (a dangling conjunctive ending would be ungrammatical)
   - `qtranslate_pl.ts`: delete `; tekst jest wysyłany do DeepL`
   - `qtranslate_pt_BR.ts`: delete `; o texto é enviado ao DeepL`
   - `qtranslate_ru_RU.ts`: delete `; текст отправляется в DeepL`
   - `qtranslate_tr.ts`: delete `; metin DeepL'e gönderilir`
   - `qtranslate_uk.ts`: delete `; текст надсилається до DeepL`
   - `qtranslate_zh_CN.ts`: delete `；文本会发送至 DeepL`

5. **Verify that exactly one tail sentence was cut per message.** Run a throwaway Python check as a
   here-document (`python3 - <<'PY'`), not a file in the repository. For each of the 14 files, read
   `/tmp/ts-before/<name>` and the working-tree file and parse `<source>`/`<translation>` of every
   `<message>` (each of these messages is on one line, so `xml.etree` or a small regex both work).
   Map the eight old sources to the eight new ones and assert:

   - 374 messages in the new file; no `type="unfinished"`, no `type="vanished"`, no empty
     translation; the eight new sources present exactly once and the eight old sources absent.
   - For each of the 112 (message, file) pairs: `old != new`; `new == new.strip()`; the last
     character of `new` is one of `. 。 । ! ?`; `norm(new)` is a prefix of `norm(old)`, where `norm`
     strips trailing whitespace and trailing punctuation `. 。 । ! ? ؛ ; ， ,`; and
     `0 < len(old) - len(new) <= 120` characters. This is the check that only the tail moved. The
     single expected exception is the Korean DeepL hint, whose ending is rewritten; verify that pair
     by eye.
   - Within each file, the tails removed from the five "third party" hints are byte-identical
     (`Der Text wird an Dritte gesendet.`, `Текст отправляется третьей стороне.`, and the localized
     equivalent in the other files). If a file shows two variants, read that file: keep the wording
     the message already had rather than inventing one.
   - Print one line per pair with the removed tail and read all 112 lines. Every tail must be the
     recipient sentence (or the DeepL joiner clause) and nothing else — no retained warning such as
     the rate limits, several requests, MyMemory's detection or Reverso's language list may appear
     in a tail.

6. **Build and test.** Prefer the `qtranslate-trixie` image for the loop and run the final Debug and
   Release check on the host. Configure `build/linux-debug` and `build/linux-release`, build both
   and grep the logs for `warning:` — `-Wall -Wextra` must stay clean. Then
   `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for the release
   tree: **46/46** in both (`ctest -N` reports 46), with `tst_translations` and `tst_settingsdialog`
   green. Windows has no MSVC here: record its Debug/Release build and 40 tests as not performed, not
   as passed; the changed strings and tests are shared.

   Final greps: the search
   `rg -n "third party|sent to DeepL|selected provider|selected instance and engine" src` returns
   only `src/services/mozhiservice.h:11`; the same search in `i18n` returns nothing (the eight English
   sources changed); in `tests` it returns only the new `!contains` assertions, which is expected.
   `git status --porcelain` shows only
   `src/ui/settingsdialog.cpp`, `tests/tst_settingsdialog.cpp` and the 14 `i18n/qtranslate_*.ts`,
   plus the roadmap and documentation files the lead maintains. Nothing under `doc/`, `README.md` or
   `README.ru.md` is edited by this task.

7. **Hand-off.** The lead updates the documentation in a separate change (`doc/architecture.md`
   §4.15 and §7, `doc/free-endpoints.md` §10, and the READMEs if the lead decides the two remaining
   limitation bullets need it); this task does not edit them. The coordinator commits (repository
   style, no Jira key, no push); the programmer commits and pushes nothing. No user action or live
   check is needed beyond the builds and tests above.

Where it is easy to go wrong:

- Retiring the sentence but leaving a stray space, a double period, a `,` before the new full stop,
  or a dangling `;`/`，`/`；` in the DeepL translation. The `new == new.strip()` and last-character
  assertions in step 5 catch this.
- Dropping `type="unfinished"` without editing the translation, or the reverse. `tst_translations`
  catches the first; step 5 catches the second.
- Editing a translation in only one of the five identical "third party" tails.
- Retyping a `<source>` in the `.ts` by hand instead of letting lupdate write it; then lrelease
  keeps a key the executable no longer uses and the UI silently falls back to English.
- "Cleaning" `src/services/mozhiservice.h:11` — that comment stays.

## Done when

- The eight hints in `src/ui/settingsdialog.cpp` match the texts in step 1 exactly; none of them says
  the text is sent to a third party, to DeepL, to the selected provider or to the selected instance
  and engine, and the provider-specific warnings (unofficial endpoint, no key, rate limits, blocks,
  several requests, MyMemory's missing detection, Reverso's six languages and missing detection, the
  fallback failure list, `None`) are intact. `rg` for the removed clauses in `src` returns only
  `src/services/mozhiservice.h:11`.
- Each of the 14 `.ts` files has 374 messages, the eight new `<source>` values, 0 `unfinished`,
  0 `vanished` and no empty translation, and every one of the 112 translations lost exactly the
  removed tail sentence and nothing else (step 5). `tst_translations` passes.
- `tst_settingsdialog` no longer asserts `third party` or `selected instance` positively, keeps the
  remaining content checks of the five affected hints, covers the DeepL and OpenAI-compatible hints
  with negative checks, and passes.
- Debug and Release build with zero warnings and all 46 Linux tests pass in both; Windows is
  recorded as not performed (no MSVC), the strings and tests are shared.
- No behaviour other than the hint text changes: no settings key, no widget, object name or layout
  change, and no edits under `doc/`, `README.md` or `README.ru.md`.

## Outcome

Done. The eight hints in `src/ui/settingsdialog.cpp` match step 1 exactly and only say what the
provider does; only the `tr()` argument changed. `src/services/mozhiservice.h:11` keeps its internal
"sends text to a third party" comment, as decided.

The 14 `.ts` files were regenerated with the Qt 6.10 `lupdate` through `update_translations` and then
edited: eight new `<source>` values, eight `<translation>` values with the recipient tail removed and
`type="unfinished"` cleared, 374 messages, 0 unfinished, 0 vanished and no empty translation in every
file. All 112 tails were checked mechanically and read one by one: every one is the recipient sentence
(or the DeepL joiner clause) and no retained warning; the Korean DeepL ending was rewritten to
`차단할 수 있습니다.` rather than cut. The five "third party" tails are byte-identical inside each
file. Tests: seven assertions inverted to `!contains`, `several requests` added for MyMemory, a
negative `sent to DeepL` check added to `deepLWebHint()`, and a new `llmHint()` slot covers the
OpenAI-compatible hint.

Host Debug and Release: 0 warnings, `ctest -N` 46, 46/46 passed, `tst_settingsdialog` and
`tst_translations` green. A Release build in the `qtranslate-trixie` image (Qt 6.8.2) also passed
46/46, so `lrelease` 6.8 accepts files written by lupdate 6.10. Windows 40 not performed (no MSVC
here); the changed strings and tests are shared.

What turned out on the way: the `lupdate` in `PATH` on this host is Qt 5.15 and would have downgraded
the `.ts`, so the CMake target (Qt 6.10) is the only safe entry point; `lupdate` also re-inserts a
changed message at its `<location>`-sorted position, which is why each `.ts` diff is larger than the
eight messages — the other 366 messages are byte-identical. The existing `build/linux-release` was
stale (45 tests) and was reconfigured.

Deferred to the lead: the documentation pass (`doc/architecture.md` §4.15 and §7,
`doc/free-endpoints.md` §10, and the two remaining limitation bullets in `README.md`/`README.ru.md`)
is a separate change; until it lands the docs still describe the old hints.

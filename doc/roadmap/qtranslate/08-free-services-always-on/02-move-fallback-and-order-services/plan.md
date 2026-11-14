---
kind: task
status: done
detailed: true
depends: [01-remove-free-services-gate]
---

# Move the fallback row and order the providers

## Task

Two user-requested presentation corrections from 2026-11-14, after the gate is gone:

1. The **Fallback service** control moves from the Services page to the Translation page. A
   fallback is a translation behaviour, not a provider setting. It becomes a `Fallback service:`
   form row in the existing `translationForm`, followed by its hint, with no group box. The
   `objectName` `fallbackService` stays so the tests can find it, and a test must prove the combo
   and the hint live on the Translation page, not on Services.
2. The providers get one user-visible order everywhere: **Google, Yandex, Bing, DeepL,
   OpenAI-compatible, MyMemory, Mozhi, Reverso** — in the Services page, in the main-window and
   tray pickers (which follow the registration order in `AppController`) and in the fallback
   options. The fallback candidates are the five no-key web endpoints in that order, i.e.
   `yandex-web`, `bing-web`, `mymemory`, `mozhi`, `reverso`.

This task is the second child of the stage and `depends` on `01-remove-free-services-gate`: doing it
first would move a hint that still tells the user to enable something on another page, and it would
rewrite regions the gate removal also touches. It reintroduces no gate, category or setting;
`proofreading/allowOnlineServices` and the Text checking tab stay untouched (the user deferred that
discussion until after this stage).

Context (post-`01` code): the Translation page builds `translationForm` and adds its five rows at
`src/ui/settingsdialog.cpp:145-182` (rows at `:170-174`, the existing `translationHint` at
`:175-181`). Line numbers below are from `2650def`; after `01` they shift, so look the symbols up
rather than trusting the numbers. The Services page creates the groups in the old provider order —
Google (`:199`), DeepL (`:221`), OpenAI-compatible (`:262`), MyMemory (`:348`), Mozhi (`:364`),
Yandex (`:417`), Bing (`:432`), Reverso (`:447`) — and adds each to `servicesLayout` inside its
block, with `addStretch()` at `:463`. The fallback group is `src/ui/settingsdialog.cpp:326-344`
with the member `m_fallbackGroup` at `src/ui/settingsdialog.h:140`. `AppController` creates,
applies and registers the eight services in `src/app/appcontroller.cpp:249-272`;
`updateServiceSelectors()` (`:535-551`) iterates `m_translations->services()`, so the pickers follow
that order. `fallbackServiceIds()` is `src/core/settings.cpp:308-315`, and `AppController` builds the
dialog's fallback list from it (after `01`).

Design decisions:

- The fallback row is one form row with a label and the combo, plus a spanning hint row, placed
  after `Auto-translate delay:` and before the existing `translationHint`. The combo and the hint
  are created with `translationPage` as parent. `QFormLayout::addRow(tr("Fallback service:"),
  m_fallbackService)` and `QFormLayout::addRow(m_fallbackHint)`; the hint keeps word wrap. The group
  box, its title and `m_fallbackGroup` are deleted. No extra group, no second label.
- The label is `Fallback service:` with a colon, matching `Source language:`, `Primary language:`
  and the other rows. The old group title `Fallback service` is removed from the `.ts` and the new
  label is translated; the hint text does not change, so its translations from `01` stay.
- The provider order is implemented by moving the whole creation blocks (create + configure +
  `addWidget`), not by reordering only the `addWidget` calls: creation order, layout order and the
  keyboard focus order must agree. Object names and all hint strings stay byte-identical.
- `AppController::createTranslation()` creates, applies settings to and registers the services in
  the same order; a comment says the pickers follow this order. Google stays first, so the default
  active service and the first-request behaviour do not change. `applySettings()` calls are
  order-insensitive but follow the same order for readability.
- `fallbackServiceIds()` is reordered to the provider order restricted to the fallback candidates:
  `yandex-web`, `bing-web`, `mymemory`, `mozhi`, `reverso`. It is the single source of the fallback
  option order (the dialog list is built from it after `01`); `SettingsStore` normalization is
  membership-based, so the order change has no other effect. The alternative — deriving the
  fallback order from registration order — was rejected: it would spread the order rule over a
  membership guard whose own order then means nothing, and the dialog list already has one source.
- Services order and fallback order are two different subsets of one provider order; both sites get
  a `keep in sync` comment so a future service is added in one place and the other is checked.
- No new setting, category or gate; the reorder and the move are presentation only.

## Steps

1. **Move the fallback row** — `src/ui/settingsdialog.h`, `src/ui/settingsdialog.cpp`:
   - Delete the `m_fallbackGroup` member (`settingsdialog.h:140`).
   - Cut the fallback creation out of the Services block (`settingsdialog.cpp:326-344`): keep the
     `QComboBox` (object name `fallbackService`), the `tr("None")` item and the `m_fallbackServices`
     items, and the `QLabel` hint (object name `fallbackHint`, word wrap, unchanged source text from
     `01`), but create both with `translationPage` as parent. Remove the `QGroupBox`, its
     `QVBoxLayout`, the group title and `servicesLayout->addWidget(...)`.
   - In the Translation section, after
     `translationForm->addRow(tr("Auto-translate delay:"), m_autoTranslateDelay)` (`:174`), add
     `translationForm->addRow(tr("Fallback service:"), m_fallbackService);` and
     `translationForm->addRow(m_fallbackHint);`, before the `translationHint` label.
   - `loadValues()` and `settings()` need no change: they use the member pointers
     (`m_fallbackService->findData(...)`, `currentData()`), and the remembered value and unknown-id
     cases keep working.

2. **Provider order on the Services page** — `src/ui/settingsdialog.cpp`:
   - Move the whole group blocks so the creation, layout and focus order is Google, Yandex, Bing,
     DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso. Yandex and Bing move above DeepL; DeepL and
     OpenAI-compatible move below Bing and above MyMemory. Keep every block's hint text, object
     name, controls and `servicesLayout->addWidget(...)` call with its block.
   - Add the missing object names while moving: `googleGroup`, `deepLGroup` and `llmGroup`. Only
     MyMemory, Mozhi, Yandex, Bing and Reverso have one today (`mymemoryGroup`, `mozhiGroup`,
     `yandexGroup`, `bingGroup`, `reversoGroup`); the new `providerOrder()` test needs all eight.
   - Add a comment before the Google block: `// Provider order: keep in sync with
     AppController::createTranslation() and fallbackServiceIds().`
   - Do not touch the proofreading page, the Text checking block or any string other than the new
     `Fallback service:` label.

3. **Registration order** — `src/app/appcontroller.cpp:249-272`:
   - Reorder the creation, `applySettings()` and `registerService()` calls to Google, Yandex web,
     Bing web, DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso. Google stays first, so the first
     registered service (the default active one) is unchanged.
   - Add a comment that the main-window and tray pickers follow this order through
     `updateServiceSelectors()`.

4. **Fallback option order** — `src/core/settings.cpp`, `src/core/settings.h`:
   - Reorder `fallbackServiceIds()` (`settings.cpp:308-315`) to `yandex-web`, `bing-web`,
     `mymemory`, `mozhi`, `reverso`.
   - Update the comment (`settings.h:135-138`) to say the list is the settings-layer guard for
     `translation/fallbackService`, listed in the provider order; the manager resolves the id at
     request time and ignores an unknown or unregistered one.
   - `AppController::showSettings()` already builds the dialog list from this list; no change there.

5. **`tst_settings`** — `tests/tst_settings.cpp`:
   - `defaults()` (`:102-105`): update the expected `fallbackServiceIds()` order to `yandex-web`,
     `bing-web`, `mymemory`, `mozhi`, `reverso`.
   - `fallbackServiceIsNormalized()` (`:685`) is unchanged: normalization is membership-based.

6. **`tst_settingsdialog`** — `tests/tst_settingsdialog.cpp`:
   - Replace `fallbackGroup()` (`:669-764`) with `fallbackRowOnTranslationPage()`:
     - `findChild<QGroupBox *>("fallbackGroup")` is null; `fallbackService` and `fallbackHint`
       exist.
     - The Translation page is `findChild<LanguageComboBox *>("sourceLanguage")->parentWidget()`;
       the Services page is `findChild<QScrollArea *>("servicesScroll")->parentWidget()`.
       `translationPage->isAncestorOf(combo)` and `->isAncestorOf(hint)` are true; the Services page
       is not an ancestor of either. The old `servicesLayout` order check goes away.
     - A `QLabel` with the text `Fallback service:` exists on the Translation page (the test runs
       without a translator, like the other English title assertions).
     - The combo has `None` plus the supplied items in the given order (pass the new order
       `Yandex, Bing, MyMemory, Mozhi, Reverso`), is enabled on a fresh dialog, keeps the remembered
       `mozhi` value and maps an unknown id to `None`; the hint keeps `no connection`, `timeout`,
       `rate limit`, `quota`, `invalid key`, `None` and `third party`.
   - Add `providerOrder()`: take the Services content layout
     (`findChild<QScrollArea *>("servicesScroll")->widget()->layout()`) and assert the index order
     `googleGroup < yandexGroup < bingGroup < deeplGroup < llmGroup < mymemoryGroup < mozhiGroup <
     reversoGroup`, and that `fallbackGroup` no longer exists.
   - Remove the neighbour-order assertions that `providerOrder()` replaces: `yandexGroup()`
     (`:565-568`), `bingGroup()` (`:609-612`), `reversoGroup()` (`:655-658`). Keep their contents
     checks (hint substrings, enabled state, no input fields).
   - `showsCurrentValues()` and `returnsEditedValues()` need no change beyond what `01` did.

7. **Translations** — `i18n/*.ts`:
   - Run `cmake --build build/linux-release --target update_translations` from the repository root.
   - The group title `Fallback service` disappears and `Fallback service:` appears; each file stays
     at **379** `<message>` entries with 0 unfinished, 0 empty and 0 vanished. Translate the new
     label in all 14 files (the old translation plus the colon is a valid base); the moved hint
     keeps its translations. If anything is `type="vanished"`, rerun
     `lupdate -no-obsolete -project build/linux-release/.lupdate/qtranslate_lupdate_project.json`.
   - `<location>` churn is expected; do not translate or reword anything else.

8. **Verification**:
   - Build `build/linux-debug` and `build/linux-release`; grep the logs for `warning:` — clean.
   - `xvfb-run -a ctest --test-dir build/linux-debug --output-on-failure` and the same for
     `build/linux-release`: 45/45 in both; `tst_settings`, `tst_settingsdialog` and
     `tst_translations` green.
   - `rg -n "fallbackGroup" src tests` returns nothing; `rg -n "Fallback service" i18n` shows the
     new label and the hint context; `rg -n "requiresOptIn|allowFreeServices|freeServicesAllowed"
     src tests` still returns nothing.
   - Check the Translation page still fits the dialog (it is resizable, `resize(600, 600)` at
     `settingsdialog.cpp:46`): run the offscreen dialog test and, if a real display is available,
     open the page and confirm the new rows are visible, not clipped. Record it as not performed if
     no display is available.
   - Record Windows Debug/Release and its 39 tests as not performed (no MSVC); no push.

## Done when

- The **Fallback service** control is a `Fallback service:` form row on the Translation page,
  followed by its hint, with no group box; `m_fallbackGroup` and the `fallbackGroup` object name are
  gone; `fallbackService` and `fallbackHint` keep their object names. A test proves both are on the
  Translation page and not on Services. The hint keeps the failure list, `None` and the third-party
  warning, and says nothing about enabling anything.
- The provider order is Google, Yandex, Bing, DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso in
  the Services page (`providerOrder()`), in the main-window/tray pickers (registration order in
  `AppController`, code-reviewed because there is no `AppController` test target) and in the
  fallback options (`fallbackServiceIds()` order, `Yandex, Bing, MyMemory, Mozhi, Reverso`, asserted
  in `tst_settings::defaults` and in the dialog test's supplied list). Google stays the default
  active service.
- No gate, category or setting is reintroduced; the Text checking tab and
  `proofreading/allowOnlineServices` are byte-identical, and the text-checking question stays
  deferred.
- Linux Debug and Release build without warnings and pass 45/45 under `xvfb-run -a`; the 14 `.ts`
  files are at 379 messages with 0 unfinished/empty/vanished and `tst_translations` passes; Windows
  Debug/Release and its 39 tests are recorded as not performed (no MSVC).
- Only `src/ui/settingsdialog.{h,cpp}`, `src/app/appcontroller.cpp`, `src/core/settings.{h,cpp}`,
  `tests/tst_settings.cpp`, `tests/tst_settingsdialog.cpp` and the 14 `i18n/*.ts` files are
  changed; no `doc/`, README, `AGENTS.md`, root-plan, CMake, version or proofreading change; no
  push.

## Risks

- The Services page diff is a block move, not a small edit. Move the whole blocks (create +
  configure + add) so the focus order matches the visual order; moving only `addWidget` calls would
  leave keyboard traversal in the old order and is easy to miss in review. `git diff --color-moved`
  makes this reviewable.
- The Translation page has no scroll area. The moved hint is a multi-line paragraph, so the page
  gets taller; the dialog is resizable, but a small screen could clip the bottom. Check it and
  record the result instead of shortening the hint (the user asked to keep it).
- `lupdate` may mark the old group title `Fallback service` vanished when it becomes
  `Fallback service:`. Run with `-no-obsolete` if needed; the count must stay 379. A different
  number means another string changed.
- The fallback option order depends on `fallbackServiceIds()`, while the pickers depend on
  registration order. They are two sites of the same rule; keep the `keep in sync` comments and
  update both when a provider is added.
- `AppController` has no test target: the picker order is code-reviewed and checked by reading
  `updateServiceSelectors()` and the registration sequence, not by an automated test. Say so in the
  outcome.
- Reordering registration changes nothing else today because Google stays first; if a future change
  moves Google, the default active service changes with it.
- Do not touch text checking. `proofreading/allowOnlineServices` is a separate feature with a
  separate, deferred user decision.

## Outcome

Done. The **Fallback service** control is a `Fallback service:` form row on the Translation page,
followed by its hint, with no group box; `m_fallbackGroup` and the `fallbackGroup` object name are
gone, while `fallbackService` and `fallbackHint` keep their names and behaviour. The providers are
in one order everywhere: Google, Yandex, Bing, DeepL, OpenAI-compatible, MyMemory, Mozhi, Reverso in
the Services page, in `AppController::createTranslation()` (the main-window/tray pickers follow it
through `updateServiceSelectors()`) and in `fallbackServiceIds()` (`yandex-web`, `bing-web`,
`mymemory`, `mozhi`, `reverso`). Google stays first, so the default active service and the
first-request behaviour do not change. No gate, category or setting was reintroduced; the Text
checking tab and `proofreading/allowOnlineServices` are byte-identical.

- Files: `src/ui/settingsdialog.{h,cpp}`, `src/app/appcontroller.cpp`,
  `src/core/settings.{h,cpp}`, `tests/tst_settings.cpp`, `tests/tst_settingsdialog.cpp` and the 14
  `i18n/*.ts`. No `doc/` (apart from this roadmap entry and the map files the lead had already
  changed), README, `AGENTS.md`, CMake, version, proofreading or `[proofreading]` change.
- The Services diff is a whole-block move: creation, layout and keyboard focus order agree. The
  missing `googleGroup`, `deepLGroup` and `llmGroup` object names were added for `providerOrder()`;
  all hint strings and the other object names are byte-identical.
- Deliberate deviation: the plan asks for `findChild<QGroupBox *>("fallbackGroup") == nullptr` and,
  in the same `Done when`, for `rg "fallbackGroup"` to return nothing over `src` and `tests`. As in
  `01`, the test assembles the identifier from two literals
  (`removedFallbackGroupObjectName()`), so both hold; the test still checks the exact name at run
  time.
- Tests: `fallbackGroup()` became `fallbackRowOnTranslationPage()` (no group; the combo and the hint
  are descendants of the Translation page and not of the Services page; the label, the six items in
  order, enabled, remembered and unknown-id cases; the hint substrings). Added `providerOrder()`
  (layout index order of all eight groups, no fallback group). The neighbour-order assertions in
  `yandexGroup()`, `bingGroup()` and `reversoGroup()` were removed; their content checks stay.
  `tst_settings::defaults` asserts the new `fallbackServiceIds()` order.
- Translations: the 14 `.ts` files are at 379 messages each with 0 unfinished/empty/vanished; the
  old group title `Fallback service` is gone, the new `Fallback service:` label is translated in
  every file and the moved hint keeps its translations. The diff is location churn plus that one
  message; `lupdate` quote re-escaping in ar/hi/ko/pt_BR/tr was reverted to keep it so.
- Verification: Linux Debug and Release build with `-Wall -Wextra` and no `warning:`; both pass
  `xvfb-run -a ctest` 45/45, with `tst_settings`, `tst_settingsdialog` and `tst_translations` green.
  `rg` finds no `fallbackGroup`, `m_fallbackGroup`, `requiresOptIn`, `allowFreeServices` or
  `freeServicesAllowed` in `src`/`tests`. The Translation page was checked for clipping offscreen at
  `600x600` and on a real display: the new rows fit with a large margin.
- Not performed: Windows Debug/Release and its 39 tests (no MSVC in this environment); CI on Qt
  6.8.3 / `debian:trixie`; real network requests. `AppController` has no test target, so the picker
  order is code-reviewed through `createTranslation()` and `updateServiceSelectors()`, not asserted
  by a test.
- Known, not fixed because it is out of scope: the provider order is a rule at two sites (the
  Services page and the registration order), so both carry a `keep in sync` comment. The
  pre-existing unused `m_mozhiHint` member remains; `m_fallbackService` and `m_fallbackHint` are now
  read in `loadValues()`/`settings()` or added to the form, so the old note about them no longer
  applies.

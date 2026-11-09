---
kind: task
status: todo
depends: [02-reverso-service]
---

# Reverso settings

## Task

Add the gate-aware `Reverso` group to the Services page of `SettingsDialog`, cover it with
`tst_settingsdialog`, and complete the 14 `.ts` files. This task changes no service behaviour and
adds no `AppSettings`/INI field: Reverso has no key and no editable URL, and the free-web gate is
already generic.

The group goes after `Bing` and before the closing stretch, with `QGroupBox(tr("Reverso"))`, the
object name `reversoGroup`, a word-wrapped hint label with the object name `reversoHint`, and no
input fields. The hint is one `tr()` string and says: Reverso is an unofficial endpoint and needs no
key; it supports only English, French, German, Spanish, Italian and Russian and cannot detect the
source language; enable free web services above to use it; it may stop working, apply rate limits or
block requests; long text is sent in several requests; the text is sent to a third party.

Add `updateReversoControls()` to `settingsdialog.h/.cpp`, modelled on `updateBingControls()`: it
enables the group only while `allowFreeServices` is checked. Call it from the settings-load path
after `updateBingControls()` and connect the `allowFreeServices` checkbox to it. Do not add a cache
reset, a validation rule or a settings key.

In `tst_settingsdialog`, add `reversoGroup()` modelled on `bingGroup()`: the group and hint exist,
the title is `Reverso`, the group is visible but disabled while the gate is off and enabled when the
gate is on, the hint contains `unofficial`, `no key`, `rate`, `block`, `free web services`,
`several requests`, `third party`, `English` and `source language`, there is no `QLineEdit`, and the
group is after `bingGroup` in the Services layout. `tst_settingsdialog` is the same target, so the
count does not change.

Run `lupdate` (the project's `update_translations` target or the equivalent `lupdate` invocation
recorded in `i18n/README.md`), then translate the two new messages — the group title and the hint —
in all 14 files. The `.ts` count moves from 311 to 313 messages per file. `tst_translations` must
pass with no unfinished, vanished or empty translations. Do not edit the READMEs, `doc/handoff.md`,
the root plan, `AGENTS.md`, `doc/architecture.md`, `doc/free-endpoints.md` or `07-fallback-chain`;
those are the lead's or `04-reverso-docs`' work.

## Done when

- The Services page has a gate-aware `Reverso` group after `Bing`, with the unofficial/no-key
  warning, the six-language limitation, the no-auto-detection statement and the opt-in hint; it has
  no key, URL or other input field.
- `updateReversoControls()` disables the group while `translation/allowFreeServices` is off and
  enables it when the checkbox is on; `settings()` exposes no new field.
- `tst_settingsdialog` has a `reversoGroup()` case that covers the title, the hint text, the gate
  state, the missing input fields and the position after the Bing group; the existing cases stay
  green.
- All 14 `.ts` files contain the two new messages (311 → 313), `tst_translations` passes, and no
  `.ts` file has an unfinished, vanished or empty translation.
- Debug and Release build on Linux without warnings; the full suite still passes 39/39 and `ctest
  -N` still shows 39 Linux tests / 34 on Windows (no new target); Windows Debug/Release and its 34
  tests are recorded as not performed (no MSVC).
- No service, `AppController`, parser, `doc/`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or
  `07-fallback-chain` file is changed; commit and push are the coordinator/lead's.

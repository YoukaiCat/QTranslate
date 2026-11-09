---
kind: task
status: done
detailed: true
depends: [02-reverso-service, 03-reverso-settings]
---

# Reverso documentation

## Task

Document the implemented Reverso service in the design documents and update the test counts. This
task changes no code and no UI: it describes what `01-reverso-parser`, `02-reverso-service` and
`03-reverso-settings` delivered.

In `doc/architecture.md`:

- §3.1: add `reversoparser.cpp/.h` and `reversoservice.cpp/.h` to the `src/services` tree.
- §3.2: add `tst_reversoparser` and `tst_reversoservice` to the test list and move the counts to
  31 shared, 3 Windows-only and 8 Linux-only tests (34 Windows, 39 Linux).
- §4.5: add `reverso` to the service-id list and `ReversoService` to the shared-queue paragraph,
  with its 600 ms pause.
- §4.7.7 (new, after §4.7.6 `BingWebService`): describe `ReversoService` — the fixed
  `POST https://cps.reverso.net/api2/TranslateText`, the JSON fields and their string values, the
  browser `User-Agent` on this request only, the six-language branch and the 36-direction cross
  product, the `auto` rejection, the flat response with
  `error`/`success`/`translation`/`directionFrom`/`directionTo`/`message`, 5000 UTF-16-code-unit
  chunks and the 600 ms pause, the gate and the error table, and the message/log hygiene.
- §4.8: add the `reverso` language branch with exactly `en`, `fr`, `de`, `es`, `it`, `ru`; note that
  the internal codes equal the service codes, that `auto` is not supported, and that the other
  services' catalogues are unchanged.
- §4.9: add the fifth opt-in service paragraph after Bing, with `translation/service=reverso` and
  the gate-off behaviour.
- §4.14: record the 39 Linux / 34 Windows counts and the shared/platform-only split.
- §8: add the two tests to the test table.
- §9.2: record the limitations — six languages, no automatic detection, 600 ms and 5000 UTF-16 code
  units are conservative assumptions, and a live smoke may be blocked or not performed.

In `doc/free-endpoints.md`:

- the status line at the top: Reverso is implemented as the fifth gated no-key service, not research;
- §4: add an `Implementation in qtranslate (2026-11-09).` block with the service id, the endpoint and
  JSON body, the browser `User-Agent`, the language branch and `auto`, the chunking and the 600 ms
  pause, the flat response with
  `error`/`success`/`translation`/`directionFrom`/`directionTo`/`message`, the error classification,
  and the tests; note that the six-language set and the 36-direction cross product come from the
  upstream mapper;
- §8: add Reverso to the implemented-service list in item 2, to the pause list in item 3
  (`Reverso 600 ms`) and to the chunk list in item 4 (5000 UTF-16 code units);
- §9: mark item 6 (Reverso) as implemented and keep it last because of the narrower coverage.

In `AGENTS.md`, update every test-count sentence to 34 Windows / 39 Linux (31 shared plus 3
Windows-only; 31 shared plus 8 Linux-only). Do not change the build, release, installer or runtime
rules.

`README.md`, `README.ru.md`, `doc/handoff.md`, the root plan and `07-fallback-chain` are lead-owned:
the READMEs' Reverso entry and the handoff note are the lead's final pass, not this task. Do not
edit them, and do not touch `doc/architecture.md` §11 or any code, test or `.ts` file.

## Done when

- `doc/architecture.md` §3.1/§3.2/§4.5/§4.7.7/§4.8/§4.9/§4.14/§8/§9.2 describe the implemented
  service, the six-language branch, the gate, the 600 ms pause, the 5000-unit chunks, the error
  table and the 34/39 counts; the new §4.7.7 is in the section order after §4.7.6.
- `doc/free-endpoints.md` status/§4/§8/§9 describe the implemented service and agree with the code
  and with `doc/architecture.md`.
- `AGENTS.md` has no stale 32/37 count and states 34 Windows / 39 Linux; no other rule changes.
- All relative Markdown links in the edited files resolve; no Cyrillic is added to the English
  documentation; `git diff --name-only` lists only `doc/architecture.md`, `doc/free-endpoints.md`
  and `AGENTS.md`.
- No code, test, `.ts`, README, `doc/handoff.md`, root-plan or `07-fallback-chain` file is changed;
  commit and push are the coordinator/lead's.

## Outcome

Done by the lead (documentation-only, no coordinator). `doc/architecture.md`
§3.1/§3.2/§4.5/§4.7.7/§4.8/§4.9/§4.14/§8/§9.2, `doc/free-endpoints.md` status/§4/§8/§9 and
`AGENTS.md` describe the implemented Reverso service.

- §3.1/§3.2 add `reversoparser.*`/`reversoservice.*`, the two tests and the `reverso/` fixtures, and
  the counts become 31 shared, 3 Windows-only and 8 Linux-only (34 Windows / 39 Linux).
- The new §4.7.7 records the fixed `POST https://cps.reverso.net/api2/TranslateText`, the seven
  string fields (`deviceId`/`appId` = `"0"`), the browser Chrome User-Agent on this request only,
  the six-language branch and 36-direction cross product, the `auto` rejection with its message, the
  flat response with `error`/`success`/`translation`/`directionFrom`/`directionTo`/`message`,
  5000 UTF-16-code-unit chunks with the 600 ms pause, the gate, the error classification and the
  message/log hygiene.
- §4.8 adds the `reverso` branch (six codes, no override table, no `auto`); §4.9 adds the fifth
  opt-in paragraph; §4.14 records 39 Linux / 34 Windows; §8 lists the two new tests and the Reverso
  rows in `tst_languages`/`tst_settingsdialog`; §9.2 records the six-language/no-detection
  limitation and the 600 ms/5000-unit assumptions.
- `doc/free-endpoints.md` marks Reverso as the fifth gated no-key service, adds the implementation
  block in §4, lists Reverso in §8 (services, pauses, chunking) and marks item 6 in §9 as
  implemented. `AGENTS.md` says 34 Windows / 39 Linux (31 shared, 3 Windows-only, 8 Linux-only) and
  no longer contains a 32/37 count.
- Verification: all relative links in the edited files resolve; `git diff --check` is clean; no
  Cyrillic was added; the only changed non-map files are `doc/architecture.md`,
  `doc/free-endpoints.md` and `AGENTS.md`. Windows 34 and a live Reverso smoke were not performed
  (no MSVC; the tests never contact the endpoint). No code, test, `.ts`, README, `doc/handoff.md`,
  root-plan or `07-fallback-chain` file was changed; the README/handoff/root-plan pass stays with the
  lead.

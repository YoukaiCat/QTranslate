---
kind: task
status: todo
depends: [01-fallback-manager, 02-fallback-settings-and-ui]
---

# Fallback documentation

## Task

Describe the implemented one-hop fallback in the design documents. This task is documentation-only
and is executed by the lead; it changes no code, test, UI or `.ts` file. It documents what
`01-fallback-manager` and `02-fallback-settings-and-ui` delivered.

In `doc/architecture.md`:

- §4.9 (`TranslationManager`, `:1311-1388`): add `fallbackService()`/`setFallbackService()` to the
  class snippet and a new paragraph after the fifth opt-in service (Reverso) covering:
  `translation/fallbackService` (empty = no fallback), the manager-level one-hop policy, the trigger
  list (`Auth`, `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`, `Network`, `Timeout`, including
  the pre-request `Auth` path when the active service is not configured), the non-trigger list
  (success, `EmptyText`, `TooLong`, `UnsupportedLanguage`, `Cancelled`, `BadResponse`), the
  requirement that the fallback is a registered opt-in service different from the reply's primary,
  the gate rule (off = no fallback even if the INI remembers one), the fallback's own `serviceId`
  in the result, the primary cache key, `clearCache()` on a fallback change and on a gate-off, the
  fallback failure returning the primary error with the fallback error logged, no second hop/loop,
  no smart secondary-language retry on the fallback, and the log/message hygiene (no text, URL,
  body or key).
- §4.11 (`:1466-1556`): add the key row `translation/fallbackService` | string: empty or one of
  `mymemory`, `mozhi`, `yandex-web`, `bing-web`, `reverso` | empty; state that it is trimmed and
  lowercased, an unknown or non-opt-in id becomes empty, it needs `translation/allowFreeServices`,
  and `settingsVersion` stays `1`. Mention that the dialog's **Fallback service** combo lists the
  registered opt-in services and is disabled while the gate is off, while the remembered value is
  kept.

In `doc/free-endpoints.md`:

- §8 (`:268-292`) item 6: replace the "fallback: official API → free web → Mozhi/another service"
  planning sentence with the implemented policy: one configured opt-in fallback, one hop, the
  trigger/non-trigger rule, the primary error when the fallback fails, the gate and the cache rule,
  and a pointer to `doc/architecture.md` §4.9/§4.11. Keep the existing "do not show raw HTML" and
  privacy rules.
- §9 (`:293-302`): record that the fallback is implemented as one configured hop after the five
  services, and why it is not an arbitrary chain (Google/DeepL free modes are modes of the same
  service; Mozhi proxies to engines, so a user can choose Mozhi as the fallback).

`AGENTS.md` already states 34 Windows / 39 Linux and needs no change because this stage adds no test
target; verify it instead of editing it. `README.md`, `README.ru.md`, `doc/handoff.md`, the root plan
and the parent stage are lead-owned: the READMEs' provider/fallback section (Google, DeepL,
OpenRouter/Local LLM as primary; MyMemory, Mozhi, Yandex web, Bing web and Reverso as opt-in free
fallbacks; MyMemory is a free API, the others are unofficial; the privacy and instability warning)
and the handoff/root-plan update are the lead's final pass, not this task.

## Done when

- `doc/architecture.md` §4.9 and §4.11 describe the implemented fallback API, the key, the trigger
  and non-trigger lists, the pre-request `Auth` path, the one-hop/no-loop rule, the primary-error
  rule, the cache rule, the gate and the privacy/log rule, and agree with the code and with
  `01-fallback-manager`/`02-fallback-settings-and-ui`.
- `doc/free-endpoints.md` §8 item 6 and §9 describe the implemented one-hop fallback and no longer
  present it as future work; the wording agrees with `doc/architecture.md`.
- All relative Markdown links in the edited files resolve; no Cyrillic is added to the English
  documentation; `git diff --check` is clean.
- `git diff --name-only` lists only `doc/architecture.md` and `doc/free-endpoints.md`; no code,
  test, `.ts`, `AGENTS.md`, README, `doc/handoff.md`, root-plan or parent-stage file is changed;
  commit and push are the lead's.

## Risks

- Do not claim a literal multi-step chain, a named chain of fallbacks or a retry of the fallback:
  the implementation is exactly one configured opt-in hop, and the documentation must say so.
- Do not move the fallback into the service descriptions as if each free service implemented it;
  it is a `TranslationManager` policy.
- The READMEs, `doc/handoff.md` and the root plan are outside this task even though the stage's
  Done-when mentions them; the lead's final pass owns them. Do not start a release or push.

---
kind: stage
status: todo
---

# Free web endpoints (old stage 13)

## Goal

Add free, no-key web endpoints as optional services: MyMemory and Mozhi, plus Yandex web, Bing web, and Reverso where feasible. DeepL free web already exists. Official APIs remain the primary services; free endpoints are fallbacks and are marked as unofficial.

## What to do, what is touched

`TranslationService` implementations and response parsers; per-host rate limits; text splitting within provider limits; fallback from official API to free web to another service; fixture and local HTTP server tests plus manual smoke tests; privacy and instability warnings in settings and docs. The old plan's stage 13 and `doc/free-endpoints.md` list the details.

## Done when

- MyMemory and Mozhi work with rate limits.
- Yandex web, Bing web, and Reverso return clear errors.
- 429/403 responses are handled without hangs.
- Official Google/DeepL remain primary and free services are fallbacks.
- Tests and documentation are updated.

## Depends on

None technically; follows stage 2 in the old order and is independent. It may start earlier if Linux is blocked.

## Risks and what is needed from the user

Unofficial endpoints have no SLA, may change or block IPs, and may have terms that restrict automation. The user must choose which endpoints to include, accept the unofficial status and privacy warnings, and run or approve live smoke tests.

---
kind: stage
status: in-progress
base: 8e9c48d
---

# DuckDuckGo and LibreTranslate as standalone providers

## Goal

Add DuckDuckGo and LibreTranslate as standalone translation providers so they behave like the
existing no-key providers: selectable in the service list, usable as the fallback service, with
settings, tests and documentation.

The research verdicts are recorded in `01-endpoint-research/notes/duckduckgo-libretranslate.md`.
DuckDuckGo is reachable only through the frontend route with a `vqd` token from the page and a browser
User-Agent; the user accepted that pattern on 2026-11-15 because Bing uses the same one. LibreTranslate
works without a key on a self-hosted server and with an optional key on a hosted instance; the user
chose a base URL plus an optional key. Both providers are implemented; nothing is dropped for
infeasibility.

## What to do, what is touched

- Research each endpoint: public protocol, auth or tokens, request and response shape, language codes
  and auto-detection, limits, terms, stability, and for LibreTranslate the self-hosted option. The
  research child `01-endpoint-research` records the findings and the raw evidence in
  `notes/duckduckgo-libretranslate.md` inside its own node; the lead copies the accepted parts into
  `doc/free-endpoints.md`, `doc/architecture.md` and both READMEs, because `doc/` is the lead's pass.
- For each provider: a `TranslationService` with a parser, chunking and pause, error mapping,
  settings (a base URL and an optional key for LibreTranslate; DuckDuckGo has no key), an id in the
  fallback list, tests (parser, service, settings) and the UI/`.ts` strings, following the existing
  no-key services.
- The research verdicts are accepted; implement the recorded protocols. If a route has changed since
  the research, stop and report instead of inventing a replacement.
- Documentation (README provider lists, `doc/free-endpoints.md`, `doc/architecture.md`) is the lead's
  pass after the code.

The work is split into `01-endpoint-research` (done), `02-duckduckgo` and `03-libretranslate`, in
that order. Each provider stage has two tasks (service, then settings/UI). No provider is dropped:
the `viable`/`not viable` branch is closed by the research and the user's 2026-11-15 decisions.

## Done when

- The research decision for both services is recorded with evidence and links.
- Each viable provider is selectable, works with an explicit source language and, where the endpoint
  supports it, with auto-detect; it can be chosen as the fallback; errors are mapped like the other
  services; tests pass.
- Both providers are implemented end to end; no half-implemented service is left in `src/`.
- Debug and Release build without warnings and all Linux tests pass; Windows is reviewed by diff.
- The user is told what was added and which limitations remain.

## Depends on

None. Uses the queued web service infrastructure from stage 3.

## Risks and what is needed from the user

- Both endpoints are unofficial and can disappear. DuckDuckGo is a token-gated frontend route with a
  body limit between 986 and 2006 characters and no terms that bless automated use; the user accepted
  it on 2026-11-15 because Bing uses the same pattern. LibreTranslate has no public key-less instance:
  the user runs a server or uses a hosted instance with a key, and its terms, quota and availability
  are the user's responsibility.
- Adding providers changes the service order, the fallback list and the settings page. Live smoke
  tests may be blocked from this network and are recorded as not performed.

---
kind: stage
status: in-progress
base: 8e9c48d
---

# DuckDuckGo and LibreTranslate as standalone providers

## Goal

Decide with evidence whether DuckDuckGo and LibreTranslate can be standalone translation providers
in QTranslate, and implement the viable ones so they behave like the existing no-key providers:
selectable in the service list, usable as the fallback service, with settings, tests and
documentation.

## What to do, what is touched

- Research each endpoint: public protocol, auth or tokens, request and response shape, language codes
  and auto-detection, limits, terms, stability, and for LibreTranslate the self-hosted option. The
  research child `01-endpoint-research` records the findings and the raw evidence in
  `notes/duckduckgo-libretranslate.md` inside its own node; the lead copies the accepted parts into
  `doc/free-endpoints.md`, `doc/architecture.md` and both READMEs, because `doc/` is the lead's pass.
- For each viable provider: a `TranslationService` with a parser, chunking and pause, error mapping,
  settings (a base URL for LibreTranslate; DuckDuckGo has no key), an id in the fallback list, tests
  (parser, service, settings) and the UI/`.ts` strings, following the existing no-key services.
- If a provider is not viable (no public protocol, scraping or cookies, no stable terms, unusable
  quality), record why and drop that child instead of implementing it.
- Documentation (README provider lists, `doc/free-endpoints.md`, `doc/architecture.md`) is the lead's
  pass after the code.

The work is split into `01-endpoint-research`, `02-duckduckgo` and `03-libretranslate`. The research
task decides each provider and is the only detailed node; the two provider stages are candidates with
two tasks each (service, then settings/UI) and are worked only after a `viable` verdict. A `not viable`
verdict drops that stage together with both of its children, and the research note is the recorded
reason. The numbers `02`/`03` are provisional: if the research recommends the other order, the lead
renames the two `todo` directories and adjusts the `depends` list of `03-libretranslate`.

## Done when

- The research decision for both services is recorded with evidence and links.
- Each viable provider is selectable, works with an explicit source language and, where the endpoint
  supports it, with auto-detect; it can be chosen as the fallback; errors are mapped like the other
  services; tests pass.
- Non-viable providers are dropped in the map with a reason; no half-implemented service is left.
- Debug and Release build without warnings and all Linux tests pass; Windows is reviewed by diff.
- The user is told which providers were added and which were not, and why.

## Depends on

None. Uses the queued web service infrastructure from stage 3.

## Risks and what is needed from the user

- Both endpoints are unofficial or volunteer-run and may be blocked or disappear. DuckDuckGo may have
  no public translation API (the Mozhi engine may be the only route), and LibreTranslate public
  instances have no stable terms; a self-hosted instance is the user's own server. If a provider needs
  scraping, cookies or a key the user does not want to share, it is dropped.
- Adding providers changes the service order, the fallback list and the settings page. Live smoke
  tests may be blocked from this network and are recorded as not performed.

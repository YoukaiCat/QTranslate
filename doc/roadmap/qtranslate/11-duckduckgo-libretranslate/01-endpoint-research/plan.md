---
kind: task
status: done
detailed: true
---

# Endpoint research: DuckDuckGo and LibreTranslate

## Task

Decide with evidence whether DuckDuckGo and LibreTranslate can be standalone translation providers in
QTranslate, and record the protocols and verdicts so that `02-duckduckgo` and `03-libretranslate` can
be implemented without probing again. This task changes no code, no test, no `.ts` file and no `doc/`
file: the only artifact is `notes/duckduckgo-libretranslate.md` in this node. The lead copies the
accepted parts into `doc/free-endpoints.md`, `doc/architecture.md` and both READMEs, because `doc/` is
the lead's pass.

The verdict rule is the stage's rule. A provider is **viable** only when

- a public HTTP route answers without a browser (no JavaScript execution) and without cookies or a
  token scraped from an HTML page, **or** the provider is a LibreTranslate server the user runs;
- no key is needed that the user does not have (a key-protected hosted instance is not a key-less
  provider);
- the terms do not forbid the use, and the route is not a personal scraping trick;
- the quality is not obviously unusable (a wrong language, gibberish, a stub answer).

Otherwise the provider is **not viable**, and the note says exactly why. A dropped provider is a normal
result, not a failure: do not bend the rule to make one work.

The house conventions the future service will follow are in `doc/free-endpoints.md` §5 and §8-§10 and
in the existing implementations: `src/services/mozhiservice.*` (a base-URL service with a per-request
language mapper and a settings group), `src/services/reversoservice.*` (a fixed endpoint with no key and
no settings), `src/services/queuedwebservice.h` (one host, serialized replies, a per-service pause),
`src/core/languages.h` (the internal catalogue and `toServiceCode`/`fromServiceCode`), and
`src/core/settings.h` (`MozhiSettings`, `fallbackServiceIds()`). Read those before deciding the shape of
the note, so the "what to implement" paragraph names real files and real seams.

## Steps

1. Read the stage, `doc/free-endpoints.md` §1-§10 and `doc/local-translation.md` §2 and §7. Write down
   the constraints the answer must satisfy: no key, no scraping, no cookie replay, no hidden instance
   rotation, `SameOriginRedirectPolicy`, a body-first error classification, no text/body/URL in logs,
   an unofficial warning in the settings hint, and one `TranslationService` per endpoint. Start the
   note with a short "Constraints" section that lists them, so the reader sees why a candidate was
   refused.

2. Probe LibreTranslate with `curl -sS -i` from the host, without a key, using neutral phrases only
   ("Hello", "Good morning", "The cat sleeps on the windowsill"). Start from the current mirror list at
   `https://docs.libretranslate.com/community/mirrors/` and probe at most three public instances, plus
   `https://libretranslate.com` as the documented hosted option. For every instance record the host,
   the date and the raw response. The probe set is:
   - `POST <base>/translate` with `Content-Type: application/json` and
     `{"q":"Hello","source":"en","target":"es"}`;
   - the same with `"source":"auto"` and a French or Russian phrase, to see whether
     `detectedLanguage.language` comes back;
   - an unsupported pair (`"source":"en","target":"xx"`, and a real pair the instance has no model
     for), to see the status and the error body;
   - `GET <base>/languages` and `GET <base>/health`;
   - one ~6000-character body, to see the size limit (`413` or a JSON error);
   - a request to an instance that requires a key, with `api_key` left out, to record the exact refusal;
   - a request with the qtranslate User-Agent and one with no `User-Agent` override, to see whether the
     server treats them differently.
   Record the exact URL, status, `Content-Type` and JSON shape for each. Stop probing instances when
   the question is answered: one instance that works without a key and one that refuses are enough.

3. Answer the LibreTranslate deployment and licence question:
   - **Public instances:** which one answered a key-less request today, with host and date; which ones
     refused and with which status/body. Do not trust a blog post or an old README for a working host.
   - **Self-hosted:** if Docker is available, start a local server (`docker run -it --rm -p 5000:5000
     libretranslate/libretranslate`, or the equivalent in `docs.libretranslate.com`) and repeat the
     probe set against `http://localhost:5000`. Record the image tag or digest and the first-start
     time. If Docker is unavailable, record "not performed" and the reason — the self-hosted path is
     still the documented option.
   - **Licence:** LibreTranslate is AGPL-3.0. Record that QTranslate stays GPL-3.0-or-later and adds no
     LibreTranslate code: the application is an HTTP client of a server the user runs. State whether
     that reading is safe and which page or licence text supports it (`doc/local-translation.md` §2 and
     §7 already record the reading).
   - **Terms:** quote or paraphrase the terms and the privacy text of the instance(s) that answered,
     and say whether automated use is allowed.
   - **Languages:** from `/languages`, the exact code list and the `targets` lists. Mark which internal
     catalogue codes have no LibreTranslate code and must be rejected as `UnsupportedLanguage`, how the
     variants look (`pt`, `pt-BR`, `zh`, `zh-Hant` and so on), and whether `source:"auto"` reports a
     detected language.
   - **Quality:** translate three neutral sentences en→ru and three ru→en and quote the results. This is
     an impression for the reader, not a benchmark; argue a drop on quality only when the output is
     outright unusable.

4. Look for a DuckDuckGo translation route that does not need a browser. Check, in this order and with
   `curl -sS -i`:
   - the search page's translate feature: `https://duckduckgo.com/?q=translate+hello+to+german` and the
     instant-answer style query, with and without `Accept: application/json`, to see whether the answer
     is JSON, HTML or a challenge;
   - the route used by community clients (SimplyTranslate, Mozhi and browser extensions): name the
     repository, the file and the commit you take it from, then probe the route it uses;
   - the Mozhi baseline:
     `GET https://<mozhi-instance>/api/translate?engine=duckduckgo&from=en&to=ru&text=Hello`, to record
     what the existing `mozhi` provider already gives the application.
   For every candidate record method, URL, headers, body, status, the response shape, whether a
   `vqd`-style token, cookie or `Referer` from a preceding page load is required, the language codes,
   and the behaviour on a long (~6000-character) body. At least three probes per candidate: plain,
   auto, and the long body.

5. Answer the DuckDuckGo terms and stability question: is translation a public API, an internal
   frontend route or an HTML scrape; does the terms-of-use page or the help page say anything about
   automated use; what happens without JavaScript (a challenge page, a redirect, an empty shell). Say
   plainly which of the three the route is, because the stage drops a scraping or cookie/token route.

6. Write `notes/duckduckgo-libretranslate.md` in English with:
   - **Verdicts:** a table with the service, `viable`/`not viable`, a one-line reason, and the map node
     it concerns (`02-duckduckgo`, `03-libretranslate`).
   - **Protocol per viable service:** method, URL, headers, body, response fields, the error shape and
     the statuses, enough for a programmer to write the parser and the service without probing again.
   - **Auth per service:** none / cookie / token / key, and exactly what is required.
   - **Language codes:** the accepted table, the internal-code mapping, `auto` support, and which codes
     must be refused.
   - **Limits:** request size, rate, quota, each marked `observed` or `assumption`.
   - **Terms and stability:** official or unofficial, who runs it, what could make it disappear.
   - **What to implement:** service id and display name, the default base URL (LibreTranslate), whether
     the provider can be configured at all without a key, the chunk size and the pause as observed or as
     a named conservative assumption, the error mapping, the settings keys, and the `isConfigured()` rule
     for an empty or invalid base URL.
   - **Appendix:** every probe with its date, command, host, status and a short response excerpt.
   Use neutral text only; never put a real user translation, a cookie or a token into the note. There
   are no secrets to use, and none may be invented.

7. Answer the map questions in the note and in the report:
   - which of `02-duckduckgo` and `03-libretranslate` is worked and which the lead must drop, and for a
     dropped one that both of its children are dropped with it;
   - the implementation order: the stage numbers put DuckDuckGo first and LibreTranslate second; say if
     the evidence recommends the other order, so the lead renames the two `todo` directories and adjusts
     the `depends` list before any code is written;
   - whether LibreTranslate can have a default base URL that works out of the box (the Mozhi precedent:
     a public default with an unofficial warning and no hidden rotation) or must start empty with
     `isConfigured() == false` and a configuration hint — with the evidence for the answer;
   - whether any viable LibreTranslate path needs an `api_key`. If it does, say so explicitly as an open
     user question (a secret field is outside this stage's scope) instead of quietly planning one.

8. Report: no build and no test run are required because no source, test, CMake or `.ts` file changes;
   say that, together with `git status` showing only the new note, the verdicts, the strongest evidence
   and every probe blocked by the network recorded as "not performed". Do not edit `journal.md`, the
   stage plan or the root plan.

## Outcome

Date: 2026-11-15. Probes: 2026-11-14 22:39-23:12 UTC. The artifact is
`notes/duckduckgo-libretranslate.md` in this node; no source, test, CMake, `.ts` or `doc/` file was
changed, so no build and no test run were performed — `git status` showed only this note and the map
files the lead had already staged for the stage split.

**Verdicts.** DuckDuckGo is **not viable**: the only browser-less route is the search frontend's
`translation.js`, which answered `403` without a `vqd` token scraped from the HTML of
`https://duckduckgo.com/?q=translate` (regex `vqd="([^"]*)"`) and `403 {"error":"Forbidden"}` with a
non-browser User-Agent, and its body limit is about 1000 characters (986 characters → `200`, 2006 →
`400 {"error":"invalid_text"}`). The same engine is already reachable through the `mozhi` provider:
`mozhi.adminforge.de` and `mozhi.pussthecat.org` both answered `engine=duckduckgo` with `200`. The
route is the token-scraping class the stage rule refuses, the terms are silent about automated
translation but the frontend route is undocumented and can vanish, and a standalone service would
re-implement Mozhi's engine under a new name.

LibreTranslate is **viable as a self-hosted server**: the LibreTranslate 1.9.6 Docker image ran on
this host and answered `POST /translate` `{"q":"Hello","source":"en","target":"ru"}` with
`200 {"translatedText":"Привет"}`, `source:"auto"` with
`{"detectedLanguage":{"confidence":90.0,"language":"ru"},"translatedText":"Good morning"}`, and
`/health` and `/languages` as expected, with no key, no cookie and no token. `/languages` of a full
instance lists 51 codes; 49 map to the internal catalogue by identity, `fil`→`tl`, `pt-PT`→`pt`, and
59 internal codes must be refused as `UnsupportedLanguage`.

**Strongest negative evidence for the public path:** none of the three documented mirrors serves a
key-less translation today. `libretranslate.com` needs a key
(`400 {"error":"Visit https://portal.libretranslate.com to get an API key"}`), `translate.fedilab.app`
answers a fixed `403` policy page to every path with curl's and with a browser User-Agent,
`translate.cutie.dating` did not accept a TCP connection (recorded as not performed, not as broken).

**Map decision.** Drop `02-duckduckgo` and both of its children with this note as the reason; keep
`03-libretranslate` and remove `02-duckduckgo` from its `depends`. No renumbering is needed. The
default base URL starts **empty** (`isConfigured() == false`, a configuration hint): no key-less
public instance answered, and `doc/free-endpoints.md` §10 forbids a public LibreTranslate instance as
the only channel. **No `api_key` is needed** for the self-hosted path; the hosted instance stays out
of scope and a secret field would be a separate user decision.

**What remains unknown.** Whether a key-less mirror answers from another network (only one vantage
point was used); the exact `vqd` lifetime and rate-limit behaviour of the DuckDuckGo route (dropped
anyway); whether the LibreTranslate image's first start completes on a network without this host's
egress stall — the models had to be installed manually here, which is recorded as an installation
note, not as a protocol defect. The AGPL-3.0 reading — QTranslate adds only an HTTP client, no
LibreTranslate code, no server binary and no container — is unchanged and already recorded in
`doc/local-translation.md` §2 and §7.

## Done when

- `notes/duckduckgo-libretranslate.md` exists in this node with a verdict per service and a protocol
  description complete enough to implement from without probing again; a `not viable` verdict carries
  the same level of evidence as a `viable` one.
- Every claim has the date and either the exact request with a response excerpt or a named source
  (repository file and commit, terms page, licence page) with the date. A probe blocked by the network
  says "not performed" and why, never "does not work".
- The note answers, per service: is there a key-less route, and does it need a cookie or a scraped
  token; the accepted source and target codes and `auto`; the size and rate limits; the terms and
  stability; for LibreTranslate the public-instance versus self-hosted answer, the local-server probe
  or its "not performed" reason, the AGPL-3.0 statement and the input for the default base URL; for
  DuckDuckGo whether `mozhi` with `engine=duckduckgo` already covers it.
- No source, test, CMake, `.ts` or `doc/` file is modified; `git status` shows only the new note.
- The report names the map decision (which provider is worked, which is dropped and with it which
  children, the order) and the open user question if a LibreTranslate key turns out to be unavoidable.

# Handoff: work state

Date: 2026-11-14. Repository: `/home/parsee/sync/mine/QTranslate`, branch `main`.

The active roadmaps are [`roadmap/qtranslate/plan.md`](roadmap/qtranslate/plan.md) (next:
`06-llm-actions`) and [`roadmap/debian/plan.md`](roadmap/debian/plan.md) (native Debian packaging,
accepted on 2026-11-14); the old plan is archived at
[`roadmap/qtranslate/legacy-plan.md`](roadmap/qtranslate/legacy-plan.md); the active documentation
is English. The migration to the tree roadmap and the English baseline is complete.

## Git

- The GitHub repository was recreated on 2026-11-06; the new repository contains only the clean
  local history and no pre-recreation commits, tags, or releases. Its releases are 0.11.1 (tag
  `v0.11.1`) and 0.12.0 (tag `v0.12.0`, published on 2026-11-09 by the tag CI). The old
  repository's history and activity are gone.
- The local history was rewritten on 2026-11-06 to replace a wrong author/committer address; all
  local commits use the GitHub noreply identity. The rewritten history was pushed to the new
  repository; no other push was made.
- The old local tags are unchanged: `v0.11.0` points to `f50c1da`, `v0.10.0` to `726dac0`; they
  were not pushed to the new repository. Only `v0.11.1` is on the remote.
- History: release 0.10.0, removal of the checksum links and return of `SHA256SUMS.txt` as an
  unlisted asset, then 0.11.0: OpenAI-compatible translation, merged DeepL, QTranslate branding.
- After 0.11.0, a hotkey fix for games and the autopause setting were added to `main`; they were
  released as the 0.11.1 Windows patch from `ef09e8c` (see Release 0.11.1).
- Inside `DeepLService` the mode is stored as `enum class Mode { Api, FreeWeb }`, and in the INI as
  the string `deepl.mode=api|free-web`. The **Services** tab in settings scrolls (`QScrollArea`) so
  the bottom hint is not cut off at 150% scaling.
- The application display name is **QTranslate**: `AppInfo::name()`, window titles, tray, User-Agent,
  `FileDescription`/`ProductName` and the installer. The internal names `qtranslate.exe`,
  `qtranslate.ini`, the autostart key `qtranslate` and the artifact names are kept for compatibility.
  The Git remote was switched to `https://github.com/youkaicat/QTranslate.git`; links in README/docs
  were updated. README got the `Translation providers` / `Провайдеры перевода` sections: Google,
  DeepL, OpenRouter (the key is required even for `openrouter/free`), local Ollama/LM Studio/llama.cpp
  and step-by-step Ollama setup.
- The working tree must be clean after completion; to verify the fix, `build\dist\bin\qtranslate.exe`
  was started with `--verbose`.

## Linux support (2026-11-08)

- Stage `02-linux` passed acceptance on 2026-11-08 over `584e722..da3fefc`. All eight children are
  `done`; `08-verification` is `done` (`b488e03`). The acceptance review found one real defect, fixed
  in `da3fefc`: an automatic save at exit deleted an unreadable secret token from the INI when the
  keyring was unavailable during load. The final gate ran Astra Debug/Release 28/28 and trixie
  Debug/Release 28/28 with no warnings; the X11 matrix on the real `:0` session passed
  (Ctrl+Q/Ctrl+Shift+Q/Ctrl+W, tray, settings, autostart, CLI with and without an instance,
  single-instance forwarding, clipboard restore). The stage criterion "published by CI on a tag" is
  satisfied by release 0.12.0 on 2026-11-09: the tag CI builds and publishes the Linux artifacts.
- X11 is the full target: `XGrabKey` hotkeys (with the Astra trusted-client step), XTest selection
  capture and paste, `_NET_ACTIVE_WINDOW`, XDG autostart, and per-user paths. Wayland is limited:
  the `GlobalShortcuts` portal where available, CLI commands, manual copying, clipboard-based
  in-place feedback, and explicit warnings; no `ydotool`/`wtype`. A real portal-capable desktop was
  not available; the portal is covered by a mock DBus (20/20) and Weston headless confirms the
  fallback.
- Tests: 45 on Linux (36 shared plus `tst_hotkeymanager_x11` 16/16,
  `tst_hotkeymanager_wayland` 20/20, `tst_selectiongrabber_linux` 12/12,
  `tst_windowactivation_linux` 13/13, `tst_hotkey_selection_x11` 7/7, `tst_autostart` 6/6,
  `tst_secretprotector` 7+1 in trixie / 8/8 on Astra, `tst_cli` 30/30,
  `tst_proofreading_selection_x11` 6/6; the shared proofreading tests are
  `tst_proofreadingmanager`, `tst_languagetoolparser`, `tst_languagetoolservice`,
  `tst_proofreadingpanel` and `tst_proofreadingpopup`). Windows keeps 39 tests (36 shared plus 3
  Windows-only) and was not run here (no MSVC). The 14 `.ts` files have 383 messages each.
- Fallback: `translation/fallbackService` (off by default, one configured opt-in service) retries a
  failed primary request once on `Auth`, `QuotaExceeded`, `RateLimited`, `ServiceUnavailable`,
  `Network` or `Timeout`; success, `EmptyText`, `TooLong`, `UnsupportedLanguage`, `Cancelled` and
  `BadResponse` never fall back, there is no second hop, and a failed fallback returns the primary
  error. The fallback result keeps its own `serviceId` and the primary cache key; changing the
  fallback or turning the gate off clears the cache. `clearCache()` also bumps a cache generation
  that each reply captures, so a result arriving after a clear is not stored; the running request
  is not aborted.
- Packaging: the repository is a native Debian source package (`debian/`, `3.0 (native)`), and
  `packaging/linux/package.sh` builds the trixie `tar.gz`, native `.deb`, AppImage and
  `SHA256SUMS-linux.txt` in `qtranslate-trixie`; the `.deb` comes from `dpkg-buildpackage` in a
  clean copy and its `Depends` from `dh_shlibdeps` (including `libsecret-1-0`/`libqt6dbus6` only
  when linked), and `packaging/linux/verify-deb.sh` gates it. `--target astra` builds
  `qtranslate-<version>-linux-x86_64-astra.tar.gz`, `qtranslate_<version>_amd64.deb`, and
  `SHA256SUMS-linux-astra.txt`; the `+astra1` suffix is gone, so the Astra and trixie `.deb` share
  the file name and need different output directories. Trixie artifacts require glibc 2.38+ and do
  not run on Astra 1.8.7; they are never claimed there. Astra artifacts are local verification
  artifacts, not CI-published. Accepted hashes are in the `07-packaging-and-ci` and
  `08-verification` outcomes.
- CI: the `Linux x64 (trixie)` job runs in `debian:trixie` (shared `trixie-packages.txt` with
  `debhelper`/`fakeroot`/`pkg-config`, Release, `timeout 300 xvfb-run -a ctest`, `package.sh` with
  the native `.deb`, `verify-deb.sh`, `qtranslate-linux-x64`); the `release` job needs both build
  jobs and publishes both artifact sets. The actual GitHub run and tag need approval.
- Paths, autostart, and secrets: `src/core/apppaths.h` uses `XDG_CONFIG_HOME`/`XDG_STATE_HOME`
  (settings `<config>/qtranslate/qtranslate.ini`, log `<state>/qtranslate/qtranslate.log`); autostart
  writes `$XDG_CONFIG_HOME/autostart/qtranslate.desktop`; secrets use libsecret (`l:<uuid>`) or a
  `0600` file with a warning (`f:<uuid>`).
- Known gaps: the AppImage `ldd` gate checks `not found` only; plugin presence is per directory;
  the printed `docker run` command lacks `--user`/`HOME=/tmp`; `debian:trixie` and
  `qtranslate-trixie` are moving tags; the Astra host gate checks `ID=astra` only; artifacts are not
  byte-reproducible. Wayland clipboard/in-place and the real portal remain unverified on a real
  compositor. Windows and the real GitHub run are not performed.
- Secrets still leave old data behind: when a secret is overwritten or migrated, the old fallback
  file and old keyring entry are never deleted (there is no `SecretProtector::forget` API), so an
  obsolete `0600` file with the plaintext can stay in `~/.config/qtranslate/secrets/`; a build
  without libsecret still drops an `l:` token from the INI by design. The tray tooltip can show the
  configured sequence before an asynchronous portal bind completes or after a desktop reassignment.
  The `secretprotector.h` comment "keeps `l:` tokens unconditionally" is inaccurate for a
  no-libsecret build.
- Release 0.12.0 is published from `main` on 2026-11-09 by user decision; the version bump, the
  README download links, `doc/releases/0.12.0.md` and this handoff are in the release commit. The
  tag CI is the build gate; Windows Debug/Release and its 39 tests are not performed locally (no
  MSVC). The old 0.11.1 GitHub release stays until the user checks the current code on Windows. The
  roadmap is past stages 3 and 5: the opt-in gate, the shared queue, MyMemory, Mozhi, Yandex web,
  Bing web and Reverso are done, the fallback chain is implemented, and stage 5 proofreading
  (LanguageTool only, no credentials) is accepted. Stage 4 (`04-local-offline-translation`) stays
  `blocked` by the user's 2026-11-05 decision; the next stage is `06-llm-actions`.

## Native Debian packaging (2026-11-14)

- The repository is a native Debian source package: top-level `debian/` with `control`, `rules`
  (`dh`, Release, `nocheck` → `QTRANSLATE_BUILD_TESTS=OFF`), `changelog` (`0.12.0`),
  `copyright` (DEP-5, GPL-3.0-or-later; the full text is in `LICENSE`),
  `source/format` (`3.0 (native)`), `source/options` (a bare `tar-ignore` line restores
  dpkg-source's default `-I` list, plus `build`, `build-*`, `out`, `.claude`), `.gitignore` and
  `README.Debian`. Roadmap: [`roadmap/debian/plan.md`](roadmap/debian/plan.md), closed on
  2026-11-14; the acceptance evidence is in its `## Outcome`.
- Build: `DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck nodoc" dpkg-buildpackage -rfakeroot -us -uc`
  from the repository root, without a trailing `.` (Astra's `dpkg-buildpackage` 1.21 rejects one).
  It writes `qtranslate_0.12.0_amd64.deb`, the `.dsc` and the `.tar.xz` to the parent directory;
  `dbgsym`, `.buildinfo` and `.changes` also appear there and are not published. `nocheck` skips
  the tests, `nodoc` leaves `README.Debian` out of the package, and without `nocheck` the rules run
  45/45 tests under `xvfb-run -a` (no trusted-client entries were needed under Xvfb; a real Parsec
  server may still need them, as `README.Debian` says). The package contains the binary, the
  desktop entry, the icon, `copyright` and `changelog.gz`, with no maintainer scripts and `Depends`
  from `dh_shlibdeps` (`libglib2.0-0` on Astra, `libglib2.0-0t64` on trixie).
- `packaging/linux/verify-deb.sh` is the content/`Depends`/install gate (15 checks and a
  `--self-test`); it derives the expected `Depends` from the host's `dpkg-shlibdeps` and the
  tree's `debian/control`, so it works on Astra and trixie. Astra: 15/15, self-test 4/4. Trixie:
  15/15, self-test 3/4 + one explicit `SELFTEST SKIP` because trixie itself requires
  `libxtst6 (>= 2:1.2.5)`.
- `packaging/linux/package.sh` builds the `.deb` through `dpkg-buildpackage` in a clean copy under
  the staging directory and publishes only the main `.deb`; the `+astra1` suffix was dropped (both
  targets build `qtranslate_0.12.0_amd64.deb`), and `dbgsym` is not published. The Linux CI job
  installs `debhelper`/`fakeroot`/`pkg-config`, builds the native `.deb`, runs the gate, keeps the
  45 tests and uploads the same `qtranslate-linux-x64` set. `build-essential` was removed from
  `Build-Depends`; `lintian` on the `.dsc` is clean apart from two
  `installable-field-mirrors-source` info tags.
- Known gaps: the portable tarball is not byte-reproducible (gzip mtime), while the `.deb` is;
  `packaging/linux/package.sh` has nine pre-existing `shellcheck` notes; the release `.deb` is not
  code-signed; old `0.11.0` artifacts remain in the local `build/artifacts-linux` and are not
  uploads.
- License: GPL-3.0-or-later, chosen on 2026-11-14. GPLv2-only was rejected because Qt 6 is
  LGPLv3/GPLv3 and OpenSSL is Apache-2.0; the current and planned dependencies are otherwise
  compatible, and the CC-BY-NC NLLB/Seamless model weights are data the project does not ship.
  `LICENSE` holds the text; the package and the bundles carry it.

## Hotkeys in fullscreen applications (after 0.11.0)

- `AppController` checks `Platform::isFullscreenForegroundWindow()` every 500 ms.
- When another application owns a fullscreen window, all combinations are released
  (`HotkeyManager::setSuspended(true)`), so the game receives Ctrl+Q/Ctrl+W. After leaving fullscreen
  they are registered again.
- A manual **Pause hotkeys** check item was added to the tray menu; it is not saved between runs and
  is needed when a game is not detected as fullscreen.
- Autopause is enabled/disabled in **Settings → Hotkeys**: `hotkeys/pauseInFullscreen`, `true` by
  default.
- The manual pause, the autopause setting and the combination entry in settings are combined.
- Verified on a borderless-fullscreen window: the log shows
  `Fullscreen foreground app detected; hotkeys paused`, after closing —
  `Fullscreen foreground app ended; hotkeys resumed`.
- Debug and Release: 23/23. The fix shipped in the 0.11.1 Windows patch from `ef09e8c`; do not
  touch the published tag or assets.

## Releases

- `v0.9.0` — the first public release, do not touch the tag and assets.
- `v0.9.1` — published: https://github.com/youkaicat/QTranslate/releases/tag/v0.9.1
  - Installer ~16.4 MB, ZIP ~22.5 MB, `SHA256SUMS.txt`; the hashes were verified.
  - The installer already has the checkboxes: Start menu (on), desktop (off), autostart (on).
  - Successful CI run of the tag: https://github.com/youkaicat/QTranslate/actions/runs/41908976474
- `v0.9.1` was built from `00f342a`, **before** the size optimization and localization. Do not
  replace its assets or move the tag.
- `v0.10.0` — published: https://github.com/youkaicat/QTranslate/releases/tag/v0.10.0
  - DeepL free web, 15 UI languages, slim distribution.
  - Locally: Debug/Release 21/21, setup 11.51 MB, ZIP 14.58 MB.
  - CI: setup 10.63 MB, ZIP 13.12 MB; install/launch/uninstall verified.
  - `SHA256SUMS.txt` remains a separate release asset; the link to it was removed from
    README/Downloads.
  - CI run of the tag: https://github.com/youkaicat/QTranslate/actions/runs/41930568365
  - The tag and assets of `v0.9.0`/`v0.9.1` were not changed.
- `v0.11.0` — published: https://github.com/youkaicat/QTranslate/releases/tag/v0.11.0
  - The tag points to `f50c1da`; `main` and the tag are pushed.
  - OpenAI-compatible provider (OpenRouter by default, free/paid models, local
    Ollama/LM Studio/llama.cpp), merged DeepL with the free-web mode, QTranslate branding, the proxy
    test fixed, scrolling added to the **Services** tab.
  - Locally: Debug/Release 23/23, setup 12 097 066 bytes, ZIP 15 333 372 bytes; the installer was
    verified by a silent install, `ProductVersion=0.11.0`, `ProductName=QTranslate`.
  - Live OpenRouter smoke through SOCKS5: `Hello, world` → `Привет, мир`.
  - CI (Qt 6.8.3 LTS): setup 11 173 822 bytes, ZIP 13 795 207 bytes; the hashes matched
    `SHA256SUMS.txt`; the CI installer was verified by a silent install, a launch without Qt in
    `PATH` and an uninstall.
  - CI run of the tag: https://github.com/youkaicat/QTranslate/actions/runs/41943741295
  - CI run of `main`: https://github.com/youkaicat/QTranslate/actions/runs/41943729918
  - `SHA256SUMS.txt` remains an unlisted asset; there is no link in README/Downloads.
- `v0.11.1` — published: https://github.com/YoukaiCat/QTranslate/releases/tag/v0.11.1
  - Windows patch from `ef09e8c`, the commit the user personally tested; branch `release/0.11.1`,
    release commit `a0f7d5e`, annotated tag `v0.11.1`.
  - Fullscreen hotkey pause, the manual **Pause hotkeys** tray item and `hotkeys/pauseInFullscreen`;
    no Linux or documentation-migration content.
  - Local Windows Debug/Release, `package.ps1` and installer checks were not performed (no MSVC);
    the tag CI built setup 11 176 182 bytes, ZIP 13 797 473 bytes and the unlisted
    `SHA256SUMS.txt`.
  - CI run of the tag: https://github.com/youkaicat/QTranslate/actions/runs/42303477455
- `v0.12.0` — published on 2026-11-09 from `main`: https://github.com/YoukaiCat/QTranslate/releases/tag/v0.12.0
  - Linux support (X11 full, Wayland limited), free web providers (MyMemory, Mozhi, Yandex web,
    Bing web, Reverso), the opt-in gate and the one-hop fallback service, the fullscreen hotkey
    pause from 0.11.1, the cache-generation fix, and the English documentation baseline.
  - Local Linux Debug/Release 39/39 with no warnings; `--version` prints `QTranslate 0.12.0`.
    Windows Debug/Release and its 34 tests were not performed (no MSVC); the tag CI was the build
    gate.
  - CI (tag `v0.12.0`): setup 11 315 684 bytes, ZIP 13 958 965 bytes, Linux `tar.gz` 28 782 608
    bytes, `.deb` 530 960 bytes, AppImage 39 410 168 bytes, plus the unlisted `SHA256SUMS.txt` and
    `SHA256SUMS-linux.txt`; every asset digest matches its checksum file.
  - CI run of the tag: https://github.com/YoukaiCat/QTranslate/actions/runs/42668400346
  - Release notes: [`releases/0.12.0.md`](releases/0.12.0.md).

## Localization (old stage 12, completed)

- UI: English (source), Russian + Spanish, German, French, Portuguese (Brazil), Italian, Polish,
  Ukrainian, Turkish, Chinese (Simplified), Japanese, Korean, Arabic, Hindi — 15 languages in total.
- All `.ts` files are in `i18n/`, 299 messages each, included in `QTRANSLATE_TS_FILES` in
  `CMakeLists.txt`; `.qm` files are embedded into `:/i18n`.
- `src/core/uilanguages.*`: codes, native names, resources; `normalize()` understands `system`,
  regional tags, `pt-PT` → `pt-BR`, `zh-CN`/`zh-Hans` → `zh-Hans`, `zh-Hant` → English;
  `canonical()` keeps the aliases (`de-DE` → `de`, `pt-PT` → `pt-BR`) in settings.
- `SettingsDialog` shows 15 languages; `AppController::applyUiLanguage()` loads the needed
  `QTranslator`, enables RTL for Arabic and sends `LanguageChange` without a restart.
- `LanguageComboBox` was switched to `AdjustToContents`, otherwise the German
  "Automatische Erkennung" was cut off.
- Added `tst_translations` (completeness of all `.ts`, `%1`/`%2`, emoji `📋 ↗ ✓ →`, leading and
  trailing spaces) and `tst_uilanguages` (code normalization and resources).
- Debug and Release: 19/19 tests at the time of localization; after DeepL free web — 21/21. A launch
  with `uiLanguage=es`, `de`, `ar` was verified: translations load, the Arabic UI is mirrored, long
  German labels are not cut off.
- Translations are a machine-assisted first pass. Selective native-speaker review is desirable before
  a public release, especially Arabic RTL and long strings in settings.
- `i18n/README.md` describes adding a language, `lupdate` and the translation rules.

## Code signing and UI Automation

- [`code-signing.md`](code-signing.md): signing options (self-signed, SignPath Foundation, Azure
  Artifact Signing, OV/EV, GitHub attestations), SignPath requirements, what to sign, Inno Setup, CI
  integration. Current releases are **not signed**; SignPath requires an OSI license, which does not
  exist yet.
- [`architecture.md`](architecture.md), §4.4: UI Automation details — `TextPattern::GetSelection`,
  the "UIA → clipboard" chain, the UIPI/UIAccess limitation; §9.2 — dictionary entries `dt=bd`
  (parts of speech, meanings, examples; short texts only).

## Free web endpoints (roadmap stage 3 (old stage 13), partial)

- [`free-endpoints.md`](free-endpoints.md): DeepL web, MyMemory, Mozhi, Yandex web, Bing web and
  Reverso are implemented; the opt-in gate (`translation/allowFreeServices`) and the shared
  `QueuedWebService` queue are implemented infrastructure. MyMemory cannot auto-detect the source
  language, so Auto-detect stays a selectable value and returns `UnsupportedLanguage` until the
  user picks a language; Reverso also requires an explicit source language (six languages, no
  `auto`). The settings hints and the errors explain it; fixing the shared language combos is a
  later decision.
- `TranslationManager` guards the cache with a generation counter: `clearCache()` increments it and
  each reply captures it, so a result that arrives after a clear (a Mozhi instance/engine change,
  `google.mode`, `deepl.mode`, `deepl.englishVariant`, a language change or a fallback/gate change)
  is returned to the caller but not stored under the current key. The active request is not aborted.
  Covered by `tst_translationmanager::resultArrivingAfterClearIsNotCached`.
- **DeepL web is implemented as a mode:** `src/services/deeplwebservice.*` and `deeplwebparser.*` are
  used inside `DeepLService` with `deepl.mode=free-web`. No key is needed; in the service list there
  is a single "DeepL", in its settings an "API / Free web" combo box; the key field is disabled in
  free-web and a warning about the unofficial status is shown. The old `service=deepl-web` migrates
  to `service=deepl` + `deepl.mode=free-web`. Verified: the service list has exactly Google
  Translate, DeepL, OpenRouter; there is no separate DeepL (free web) group.
- `POST https://oneshot-free.www.deepl.com/v1/translate`, `Authorization: None`, JSON; 5000 characters
  per chunk, requests are serialized and sent at most once every 2 s, on `429` one retry after 3 s.
  `403` → `RateLimited`, `413` → `TooLong`, `5xx` → `Network`.
- Language codes: base ones in lowercase (`DE` → `de`), regional — `pt-BR`, `en-US`, `zh-Hans`. The
  endpoint silently returns an uppercase base code without translation, so the normalization is
  mandatory. `DE`, `PT-BR`, `EN-US`, `zh-Hans` were verified with live requests.
- `tst_deeplwebparser`, `tst_deeplwebservice` and `tst_deeplservice` (`free-web` mode); Debug and
  Release: 23/23 tests at that time (the suite is 39 Windows / 45 Linux now).
- Live smoke through the built `DeepLWebService`: `Hello, world` → `Hallo, Welt`,
  `detected_source_language=en`, no errors.
- **Bing web is implemented:** `src/services/bingwebparser.*` and `bingwebservice.*`; the token page
  yields `IG`/`data-iid`/key/token/lifetime/MUID, the POST is urlencoded with the explicit `MUID`
  cookie, the token+MUID cache lasts the page lifetime, a 401 refreshes once, 403 is a block, and the
  pause/chunk are 1000 ms/5000 UTF-16; tests `tst_bingwebparser`/`tst_bingwebservice`; the live
  smoke passed (`Hallo Welt`, detected `en`).
- **Reverso is implemented:** `src/services/reversoparser.*` and `reversoservice.*`; the fixed
  `POST https://cps.reverso.net/api2/TranslateText` sends a seven-field JSON body (all strings,
  `deviceId`/`appId` = `"0"`) with a browser Chrome UA on this request only. Six languages
  (`en`, `fr`, `de`, `es`, `it`, `ru`), no `auto` (a dedicated `UnsupportedLanguage` message), and a
  36-direction cross product. `success: false`/`error: true` are errors even under HTTP 200;
  `directionFrom` fills `detectedSourceLang` and `directionTo` is tolerated. The pause/chunk are
  600 ms/5000 UTF-16; tests `tst_reversoparser`/`tst_reversoservice`; a live smoke was not run (the
  tests use a local server and the endpoint was not contacted in this environment).
- Yandex web: `api.browser.yandex.com/instaserp/translate`, form-urlencoded; Reverso:
  `cps.reverso.net/api2/TranslateText`; Mozhi: `/api/translate` on a public/own instance; MyMemory:
  `api.mymemory.translated.net/get`, ~500 bytes per request, the source language is required.
- All except MyMemory and self-hosted Mozhi/LibreTranslate are unofficial: no SLA, possible `429`,
  HTML/API changes and terms-of-service restrictions. They need a rate limit and a fallback.
- **DeepL API Developer** (formerly Free): 1 000 000 characters **in total**, no monthly reset; after
  the limit — Growth/Pro. The earlier information about 500 000 per month is outdated.

## Local offline translation (roadmap stage 4 (old stage 14), option)

- [`local-translation.md`](local-translation.md): the option of Argos models + CTranslate2 without
  Python. The code is **not implemented**; the work is roadmap stage 4 (old stage 14).
- Argos Translate is a Python library, `pip install argostranslate` / `argospm install`; `.argosmodel`
  models are a zip with a CTranslate2 model, `sentencepiece.model` and `metadata.json`. Suitable for a
  prototype but not for shipping: Python + spaCy/Stanza are required.
- For the product the native CTranslate2 path with `.argosmodel` models is preferable; an example is
  MetalTranslate (MIT, experimental). Needed: a model manager, SHA-256, safe unpacking, sentence
  segmentation, a pivot through English and a fallback to the cloud.
- Limitations: quality below DeepL/Google, the Argos index is mostly `X ↔ en`, model size tens to
  hundreds of MB, model licenses must be checked separately. NLLB/Seamless are CC-BY-NC, not for
  commercial distribution without checking.
- ONNX is a format, not an engine; ONNX Runtime is needed, and GenAI for LLMs. It works on CPU and
  Linux; DirectML is Windows-only. ONNX GenAI is convenient with DirectML/OpenVINO/a ready ONNX
  model, but for a local LLM llama.cpp + GGUF is simpler: more ready models, one file, a simpler
  build. Gigabytes are the model, not the engine; for both options 4B/7B take 2.5–5 GB.
- Next step: a PoC with Python Argos (en↔ru, en↔de, pivot ru→de), measuring quality/speed/RAM; if
  acceptable — a native CTranslate2 runner.

## Proofreading (roadmap stage 5 (old stage 15), implemented)

- [`proofreading.md`](proofreading.md): implemented in stage 5 over `eb91f6b..263091f`, with the
  acceptance fix `0ca1ad2`. The first version ships **LanguageTool only** (user decision of
  2026-11-09); Hunspell/Nuspell, Yandex Speller and the LLM checker are deferred. The premium
  LanguageTool credentials are out of scope: the public API rejects them and can echo a submitted
  key, so the application sends no `apiKey`/`username`.
- The provider-independent layer is `core/proofreadingtypes`, `proofreadingservice` (a
  `ProofreadingReply` with the `TranslationReply` semantics), `proofreadingmanager` (channels
  `MainWindow`/`InPlace`, registry, pre-network refusals) and `proofreadingtext` (applying issue
  replacements). `LanguageToolService` uses `RequestQueue`, posts `text`, `language` and
  `enabledOnly=false` to the public or self-hosted `v2/check`, chunks at 20 000 UTF-16 units with
  offset shifts, filters categories client-side, supports `auto`, and maps 401/403→`Auth`,
  413→`TooLong`, 429→`RateLimited`, 5xx→`Network`.
- A separate **Text checking** tab and independent `[proofreading]` keys; `allowOnlineServices` is
  the privacy gate (off by default) and covers every remote endpoint. `checkBeforeTranslation`,
  `autoReplace` and the proofreading hotkey are off/empty by default.
- The main window has **Check**, issue highlighting, Replace/Ignore, Replace all with a preview,
  Copy corrected text, one-undo-step replacements, and check-before-translation confirmation.
  `ProofreadingPopupWindow` is a non-activating external-selection confirmation with
  Replace selection/Copy/Cancel; the flow uses `Channel::InPlace`, a foreground watchdog, clipboard
  fallback and the shared paste path. The fifth `HotkeyAction::ProofreadSelection` is empty by
  default; the same scenario is `--proofread-selection`.
- Verification: Linux Debug/Release 45/45 with no warnings, `.ts` 383, Windows 39 not performed
  (no MSVC). Known gaps: a click on the non-activating popup under a real window manager is
  unverified and falls back to the clipboard on a focus change; the grabber waits a fixed 400 ms
  for the clipboard; the `QSKIP` for a missing `DISPLAY` is unreachable because `QTEST_MAIN` creates
  the xcb application first; a paste-shortcut failure reuses the shared "translation" wording;
  disabling the online gate during a running long check does not abort the remaining chunks; the
  all-categories-unchecked case is normalized back to all four.

## LLM actions (roadmap stage 6 (old stage 16), partially implemented)

- **Translation through an OpenAI-compatible provider is implemented:** the `llm` service, the
  `OpenAiCompatibleService` class, the `src/ai/openaichat.*` transport. OpenRouter is the default
  address; in settings `baseUrl`, `apiKey` (DPAPI) and `model` are set, and model loading is
  available.
- The service name is derived from the host: `openrouter.ai` → "OpenRouter", `api.openai.com` →
  "OpenAI", a local address → "Local LLM", otherwise the host. This is one adapter, not a class per
  provider.
- Prompt: a translator system role, the text as data, JSON `{"detected_language","translation"}` for
  auto; Markdown wrappers and plain text — fallback.
- 401/403/402/429/413/5xx errors and a timeout map to `TranslationError`; text/keys are not logged.
- `settings.llm` in the INI — `[llm]`; changing the model/base URL clears the translation cache.
- Tests: `tst_openaichat`, `tst_openaicompatibleservice`; Debug and Release — 23/23, `/W4` without
  warnings.
- A live OpenRouter smoke was done through the SOCKS5 proxy `127.0.0.1:2080`: `Hello, world` →
  `Привет, мир`, HTTP 200 in 6.6 s, model `openrouter/free` (tokens 125 + 301). The proxy test in
  the app was fixed: the old URL `translate.googleapis.com` returned 404 and produced a false error;
  now `https://www.gstatic.com/generate_204`, and any HTTP response other than 407 is considered a
  success.
- [`llm-actions.md`](llm-actions.md) was updated: translation is considered ready, the other actions
  are planned.
- The dictionary was moved to a separate design [`dictionary.md`](dictionary.md) and roadmap stage 7
  (old stage 17): LLM, Google `dt=bd`, a CSV glossary, Wiktionary; `DictionaryService`, not
  `TranslationService`.
- `LlmProvider`/`AiAction` for future actions: summarization, rewriting, explanation, pronunciation,
  checking; the result is a preview/diff, replacement only after confirmation.
- LLM does not replace DeepL/Google/LanguageTool by default. ASR/TTS/OCR are separate future tasks.

## Distribution size

Local 0.10.0 build (Qt 6.12):

- the installed `build/dist` — 35.0 MB;
- `qtranslate-0.10.0-win64-setup.exe` — 11.51 MB;
- `qtranslate-0.10.0-win64.zip` — 14.58 MB;
- the ZIP has 41 entries, 13 Qt translations; `opengl32sw.dll` is absent.

CI 0.10.0 build (Qt 6.8.3 LTS):

- `qtranslate-0.10.0-win64-setup.exe` — 10.63 MB;
- `qtranslate-0.10.0-win64.zip` — 13.12 MB;
- the installer, the ZIP and `SHA256SUMS.txt` were verified; the file remains an unlisted asset.

Qt translations for 13 languages add ~2.4 MB to dist, ~0.32 MB to the installer and ~0.68 MB to the
ZIP. `qt_de.qm` is loaded from `build/dist/translations` without Qt in `PATH`; the log shows
`Loaded Qt translation "qt_de.qm" from ...`. For Hindi, Qt 6.12 has no `qt_hi.qm`, so standard
dialogs stay English.

What was removed:

- `opengl32sw.dll` (~19.7 MB) — the application does not use OpenGL/Qt Quick/QGraphicsView; Qt
  Widgets works through raster. Launch without the library was verified;
- all Qt translations except `en/ru` (there were 33 `.qm`, 5.7 MB) — at `99ffdf4`. In localization,
  Qt translations were returned for `ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar` (Hindi is absent
  in Qt 6.12).

There are no debug PDB/debug DLLs in the distribution; the Qt libraries are Release.

**Important:** the local `0.11.0` artifacts are built for verification; the published assets are the
CI assets built from the tag. Do not upload the local files manually.

## Release 0.10.0 (2026-11-05)

1. The version was bumped in `CMakeLists.txt` and `resources/qtranslate.rc`; README,
   [`releases/0.10.0.md`](releases/0.10.0.md), the old plan (now the historical archive
   [`roadmap/qtranslate/legacy-plan.md`](roadmap/qtranslate/legacy-plan.md)) and
   [`architecture.md`](architecture.md) were updated.
2. Debug and Release: 21/21, `/W4` without warnings.
3. `package.ps1` produced setup 11.51 MB, ZIP 14.58 MB and `SHA256SUMS.txt`; the files were verified.
4. The installer was verified: silent install, `FileVersion`/`ProductVersion` 0.10.0, launch without
   Qt in `PATH`, uninstall; the settings and `HKCU\Run` were restored.
5. `main` and the annotated tag `v0.10.0` were pushed; the CI run was successful, the assets were
   downloaded and verified.
6. Release: https://github.com/youkaicat/QTranslate/releases/tag/v0.10.0

## Release 0.11.1 (2026-11-07)

The user chose **0.11.1 from `ef09e8c` now**, then **0.12.0 after the Linux stage**. `ef09e8c` is
the commit the user personally tested; it is three commits after `v0.11.0` (`a6482e7` record,
`d7d4d05` release hotkeys for fullscreen, `ef09e8c` fullscreen pause setting).

1. The release branch `release/0.11.1` was created from `ef09e8c`; current `main` was not tagged
   and the documentation migration/Linux work is not included. The branch was deleted after
   publication; the tag and release stay on `a0f7d5e`.
2. `a0f7d5e` bumped the version in `CMakeLists.txt` and `resources/qtranslate.rc`, added
   `doc/releases/0.11.1.md` (fullscreen hotkey pause, the manual **Pause hotkeys** tray item,
   `hotkeys/pauseInFullscreen`, no Linux claims) and updated `README.md`/`README.ru.md`.
3. Local Windows Debug/Release, `package.ps1` and installer checks were not performed (no MSVC);
   the tag CI was the build gate.
4. The branch and the annotated tag `v0.11.1` were pushed; CI succeeded and published the installer,
   portable ZIP and `SHA256SUMS.txt` (unlisted). No old tag was pushed or recreated.
5. Release: https://github.com/YoukaiCat/QTranslate/releases/tag/v0.11.1

User decision (2026-11-08): leave 0.11.1 exactly as published; do not re-tag or re-release it.
Releases are tag-driven. 0.12.0 is published; the old 0.11.1 GitHub release stays until the user
checks the current code on Windows.

## Release 0.12.0 (2026-11-09)

The user asked for the release in the previous session and confirmed it on 2026-11-09 after the
journal's earlier 2026-11-08 deferral. 0.12.0 comes from `main`; the release commit bumps the
version in `CMakeLists.txt` and `resources/qtranslate.rc`, adds `doc/releases/0.12.0.md` and updates
both READMEs, this handoff and the roadmap journal.

1. Local Linux Debug/Release: 39/39 with no warnings under `xvfb-run -a`; `--version` prints
   `QTranslate 0.12.0`.
2. Windows Debug/Release, `package.ps1` and the installer were not performed (no MSVC); the tag CI
   is the build gate. No local artifacts are uploaded manually.
3. `main` and the annotated tag `v0.12.0` were pushed; the CI `linux` job built the trixie
   `tar.gz`, `.deb` and AppImage, the `windows` job built the installer and ZIP, and the `release`
   job published all of them with `SHA256SUMS.txt`/`SHA256SUMS-linux.txt` (unlisted). The tag run
   and the `main` run both succeeded; every asset digest matches its checksum file.
4. The old 0.11.1 GitHub release stays published until the user checks the current code on Windows
   (user decision of 2026-11-09, superseding the earlier timing).
5. The next milestone is stage 6 (`06-llm-actions`); stage 5 (`05-proofreading`) is accepted and its
   documentation is updated.

## User rules

- Do not push every commit; GitHub Actions is under load. Push only on request or when shipping an
  agreed release.
- Agree the release and the version in advance.
- Do not modify old tags and releases; fixes go into a new release.
- README: Main features before the screenshots; exact file names must be links; keep the style brief.
- Do not list `SHA256SUMS.txt` and other checksum files in README/Downloads; keep the file itself in
  the release assets for those who need the hashes.
- Commit with `Vladislav Mileshkin <1074478+youkaicat@users.noreply.github.com>`; the project-local
  `user.email` is set to it. Never use a work address in commits or tags.

## Known manual gaps

These are known manual checks that were never completed. They come from environment and account
limitations, not from confirmed defects, and they do not block new tasks. The background is in the
archived plan [`roadmap/qtranslate/legacy-plan.md`](roadmap/qtranslate/legacy-plan.md) and
[`architecture.md`](architecture.md) §9.2.

- Tray behaviour: the code is ready, the manual tray check was not done — old stage 1.
- Selection capture in Word, VS Code and Telegram: Word and VS Code are not installed; Telegram is
  authorized, but private chats were not checked — old stage 3.
- The tray notification about a busy hotkey: rejection of registration is covered by
  `tst_hotkeymanager`, the visual notification was not verified — old stage 3.
- A Win+V history entry during capture: the intermediate Ctrl+C may leave an entry; only clipboard
  restoration is protected — old stage 3, §9.1.
- A real second monitor with a different scale: one 3840×2160 monitor at 175% was tested — old
  stage 4.
- Autostart after a real Windows reboot: the `Run` entry and the `--minimized` flag were verified, the
  reboot was not — old stage 5.
- Real successful DeepL and Google Cloud keys: no working keys were available; the protocol and the
  errors are covered by local tests — old stage 6.
- SOCKS5 and an external proxy with authentication: local HTTP/SOCKS5 with authentication is covered
  by `tst_networkmanager`, a real external server was not tested — old stage 7.

## Commands and gotchas

- Fast local build: `cmd /c build\cli-build.cmd Debug` / `Release`. The `build/cli-build.cmd` script
  is not in git and is hardcoded to `C:\projects\qtranslate`, Qt 6.12 and VS 2022. Do not use it in a
  worktree.
- A bare `build\cli-Release\qtranslate.exe` does not start on a double click without Qt in `PATH`
  (Entry Point Not Found). For a manual run use `build\dist\bin\qtranslate.exe` after `package.ps1`
  or add `C:\Qt\6.12.0\msvc2022_64\bin` to `PATH`.
- One test: `ctest --test-dir build/cli-Release -R tst_name --output-on-failure`.
- CI uses Qt 6.8.3 LTS because aqtinstall 3.3.0 cannot install Qt 6.12.
- `packaging/windows/qtranslate.iss` is UTF-8 with BOM; do not lose the BOM.
- In Inno `GetIniBool` did not parse `false`; `GetIniString` is used. The autostart task is found by
  caption (`FindAutostartItemIndex`), not by the task name in `TasksList.Items`.
- Installer tests write to the real `%APPDATA%`, HKCU Run and the Start menu; do a backup/restore.
  Stop qtranslate before installing/rebuilding.
- Free Google requires HTTP/1.1 (`Http2AllowedAttribute=false`), otherwise systematic 429.
- Secrets use DPAPI; logs must not contain texts/keys/passwords.

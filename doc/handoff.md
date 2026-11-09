# Handoff: work state

Date: 2026-11-09. Repository: `/home/parsee/sync/mine/QTranslate`, branch `main`.

The active roadmap is [`roadmap/qtranslate/plan.md`](roadmap/qtranslate/plan.md); the old plan is
archived at [`roadmap/qtranslate/legacy-plan.md`](roadmap/qtranslate/legacy-plan.md); the active
documentation is English. The migration to the tree roadmap and the English baseline is complete.

## Git

- The GitHub repository was recreated on 2026-11-06; the new repository contains only the clean
  local history and no pre-recreation commits, tags, or releases. Its only release is 0.11.1
  (tag `v0.11.1`). The old repository's history and activity are gone.
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
  single-instance forwarding, clipboard restore). The stage criterion "published by CI on a tag" was
  not performed: the user deferred the 0.12.0 release on 2026-11-08, and the stage was closed with
  this recorded as not performed.
- X11 is the full target: `XGrabKey` hotkeys (with the Astra trusted-client step), XTest selection
  capture and paste, `_NET_ACTIVE_WINDOW`, XDG autostart, and per-user paths. Wayland is limited:
  the `GlobalShortcuts` portal where available, CLI commands, manual copying, clipboard-based
  in-place feedback, and explicit warnings; no `ydotool`/`wtype`. A real portal-capable desktop was
  not available; the portal is covered by a mock DBus (20/20) and Weston headless confirms the
  fallback.
- Tests: 37 on Linux (29 shared plus `tst_hotkeymanager_x11` 16/16,
  `tst_hotkeymanager_wayland` 20/20, `tst_selectiongrabber_linux` 12/12,
  `tst_windowactivation_linux` 13/13, `tst_hotkey_selection_x11` 7/7, `tst_autostart` 6/6,
  `tst_secretprotector` 7+1 in trixie / 8/8 on Astra, `tst_cli` 20/20; `tst_queuedwebservice`,
  `tst_mymemoryparser`, `tst_mymemoryservice`, `tst_mozhiparser`, `tst_mozhiwebservice`,
  `tst_yandexwebparser`, `tst_yandexwebservice`, `tst_bingwebparser`, and `tst_bingwebservice` are
  the new shared tests). Windows keeps 32 tests and was not run here (no MSVC).
- Packaging: `packaging/linux/package.sh` builds the trixie `tar.gz`, `.deb`, AppImage, and
  `SHA256SUMS-linux.txt` in `qtranslate-trixie`; the `.deb` uses `dpkg-shlibdeps` (including
  `libsecret-1-0`/`libqt6dbus6` only when linked). `--target astra` builds
  `qtranslate-<version>-linux-x86_64-astra.tar.gz`, `qtranslate_<version>+astra1_amd64.deb`, and
  `SHA256SUMS-linux-astra.txt`. Trixie artifacts require glibc 2.38+ and do not run on Astra 1.8.7;
  they are never claimed there. Astra artifacts are local verification artifacts, not CI-published.
  Accepted hashes are in the `07-packaging-and-ci` and `08-verification` outcomes.
- CI: the `Linux x64 (trixie)` job runs in `debian:trixie` (shared `trixie-packages.txt`, Release,
  `timeout 300 xvfb-run -a ctest`, `package.sh`, `qtranslate-linux-x64`); the `release` job needs
  both build jobs and publishes both artifact sets. The actual GitHub run and tag need approval.
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
- Release 0.12.0 is deferred by user decision on 2026-11-08. When it happens, the version bump, the
  README download links, and `doc/releases/0.12.0.md` are needed; the old 0.11.1 GitHub release is
  deleted then (user decision). The roadmap is in stage 3: the opt-in gate, the shared queue,
  MyMemory, Mozhi, Yandex web and Bing web are done; the Bing stage is accepted and `06-reverso`
  is next.

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

- [`free-endpoints.md`](free-endpoints.md): DeepL web, MyMemory, Mozhi, Yandex web and Bing web are
  implemented; the opt-in gate (`translation/allowFreeServices`) and the shared `QueuedWebService`
  queue are implemented infrastructure; Reverso is research and the next roadmap task. MyMemory cannot auto-detect the source language, so Auto-detect stays a selectable value and
  returns `UnsupportedLanguage` until the user picks a language; the settings hint and the error
  explain it. Fixing the shared language combos is a later decision.
- A deferred cache problem affects Mozhi and the other setting changes: an active reply that finishes
  after `clearCache()` can store a result fetched from the old instance/engine under the current
  cache key. The fix is a cache-generation counter in `TranslationManager` (capture it in the reply
  and skip `storeCached()` for an old generation) plus a test; the same applies to `google.mode`,
  `deepl.mode`, and `deepl.englishVariant`. This is a follow-up, not fixed in the Mozhi stage.
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
  Release: 23/23 tests at that time (the suite is 32 Windows / 37 Linux now).
- Live smoke through the built `DeepLWebService`: `Hello, world` → `Hallo, Welt`,
  `detected_source_language=en`, no errors.
- **Bing web is implemented:** `src/services/bingwebparser.*` and `bingwebservice.*`; the token page
  yields `IG`/`data-iid`/key/token/lifetime/MUID, the POST is urlencoded with the explicit `MUID`
  cookie, the token+MUID cache lasts the page lifetime, a 401 refreshes once, 403 is a block, and the
  pause/chunk are 1000 ms/5000 UTF-16; tests `tst_bingwebparser`/`tst_bingwebservice`; the live
  smoke passed (`Hallo Welt`, detected `en`).
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

## Proofreading (roadmap stage 5 (old stage 15), planned)

- [`proofreading.md`](proofreading.md): a separate mode for spelling, grammar and punctuation
  checking; the code is not implemented.
- A separate **Text checking** settings tab and independent `proofreading/*` keys; checking must not be
  part of `TranslationService`.
- Providers: LanguageTool (cloud/self-hosted, grammar/punctuation), Hunspell/Nuspell (spelling),
  Yandex Speller (spelling ru/uk/en), LLM — optionally through a diff.
- `ProofreadingIssue` with UTF-16 `offset`/`length`, `replacements`, `ruleId`, `category`, `severity`;
  replacement in the main window with undo, in an external application — with confirmation.
- `checkBeforeTranslation` and `autoReplace` default to `false`; the checked text is not logged.

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
Releases are tag-driven. When 0.12.0 is published, delete the old 0.11.1 GitHub release.

## Next release

**0.12.0 is deferred by user decision on 2026-11-08**; the Linux stage is done and stage 3 is in
progress. When it is approved, it comes from `main` with the version and scope agreed again. It
follows the same release flow: version bump, `doc/releases/0.12.0.md`, README links, local checks
where possible, tag CI as the build gate, and an explicit user approval before the push. The 0.11.1
branch is deleted; do not recreate it. When 0.12.0 is published, delete the old 0.11.1 GitHub
release.

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

# Handoff: work state

Date: 2026-11-07. Repository: `C:\projects\qtranslate`, branch `main`.

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

## Linux baseline (2026-11-06, updated 2026-11-07)

- The Linux tree builds, links, and passes 28 of 28 tests in Debug and Release on Debian 13
  (trixie, Qt 6.8.2, GCC 14.2) and Astra Linux 1.8.7 (Qt 6.10.2, GCC 12.2; Clang 16.0.6), with
  `-Wall -Wextra` and no warnings. `qtranslate --version` works. Windows builds and the 23 tests
  were not performed here.
- `packaging/linux/trixie.Dockerfile` reproduces the container; it includes `xauth` because
  `xvfb-run` needs it. Task 07 must keep the package list and add the packaging tools.
- Packaging measurement (task 07 planning): trixie Qt 6.8.2 `libQt6Gui`/`libQt6Network` require
  `GLIBC_2.38`, while Astra 1.8.7 has glibc 2.36; `dpkg-shlibdeps` produces `libglib2.0-0t64` and
  `libxtst6 (>= 2:1.2.5)`, while Astra has `libglib2.0-0` 2.74.6 and `libxtst6` 2:1.2.3. The trixie
  `.deb`, tar.gz and AppImage therefore cannot serve Astra; only a native Astra build works. Task 07
  is a stage with six children: desktop/install, tar.gz, `.deb`, AppImage, native Astra artifacts,
  and CI. Only trixie artifacts are CI-published; the Astra artifacts are local verification
  artifacts, because release artifacts stay CI-built. Qt's Linux deploy script copies no Qt
  libraries and writes a trixie `qt.conf`, so it is guarded with `if(WIN32)` and `package.sh` owns
  the layout.
- Per-user paths, autostart and secrets are implemented (task 05, `aed4028`). `src/core/apppaths.h`
  reads `XDG_CONFIG_HOME`/`XDG_STATE_HOME` directly (empty and relative values ignored) and appends
  lowercase `qtranslate`; settings are `<config>/qtranslate/qtranslate.ini` and the log
  `<state>/qtranslate/qtranslate.log`, while a portable `qtranslate.ini` next to the executable
  still wins. Autostart writes `$XDG_CONFIG_HOME/autostart/qtranslate.desktop` with Desktop Entry
  quoting, `--minimized`, repair and removal; the writer is native Qt, no GLib/GIO or KDE/KF6
  dependency is added, and libsecret stays the only optional transitive GLib dependency (user
  decision). Secrets use libsecret when `libsecret-1` is found at
  build time and `org.freedesktop.secrets` is available (`l:<uuid>`), otherwise a `0600` file under
  `<config>/qtranslate/secrets` with a `0700` directory (`f:<uuid>`) and a visible warning in the
  settings dialog; a capability drop during save shows a message box.
  `SecretProtector::canReuse()` keeps an unchanged `l:` token through a transient keyring outage,
  migrates `f:` to libsecret and re-protects missing or corrupt tokens. Task 07 must add
  `libsecret-1-0` as a runtime dependency when the package is built with libsecret; a build without
  it remains supported.
- Known task 05 issues not fixed in this task: an undecodable or unknown token is dropped from the
  INI on the next save (a no-libsecret build or a transient keyring failure at load); the fallback
  hint does not highlight an already stored `f:` token while the keyring is available (it migrates
  on the next save); test entries accumulate in the Astra keyring (no delete API); a path containing
  `\` or `%` can break the autostart `Exec=` parser (the plan-prescribed escaping was kept). Also:
  the fallback file stays under XDG config in portable mode, old keyring/file entries remain after
  an overwrite or migration, and the D-Bus probe can be a false negative when the service is
  installed but not running or activatable.
- The Linux install now guards the Windows deploy block with `if(WIN32)`: the generic deploy tool
  copies no Qt libraries and writes a trixie-specific `bin/qt.conf`, so the packaging script owns
  the bundle. One `xvfb-run -a` run hung before the command started in the container; CI should
  wrap tests in `timeout`.
- Desktop entry and install layout (task 07.01, `cc5fcac`): `packaging/linux/qtranslate.desktop`
  passes `desktop-file-validate`; CMake installs it to `share/applications` and
  `resources/icons/app.png` as `share/icons/hicolor/256x256/apps/qtranslate.png`;
  `QGuiApplication::setDesktopFileName("qtranslate")` fixes the Wayland app id; the Linux install
  tree has only `bin/qtranslate`, the desktop file and the icon, and keeps
  `RUNPATH $ORIGIN:$ORIGIN/../lib`.
- Astra Linux Parsec denies passive `XGrabKey` for untrusted binaries. The user approved the `KBD_R`
  trusted-client exception: `/etc/X11/trusted.d/qtranslate` lists
  `build/linux-{debug,release}/qtranslate`, `build/linux-{debug,release}/tests/tst_hotkeymanager_x11`
  and `build/linux-{debug,release}/tests/tst_hotkey_selection_x11`, and
  `sudo /usr/bin/fly-set-execid.sh` sets `security.exec_id`. With the attribute set, Debug and
  Release pass 28/28, the X11 hotkey test passes 16/16 on Astra, and the real `:0` session registers
  and activates the hotkeys. The attribute is lost on rebuild; re-run the script after building.
- Linux hotkeys: X11 `XGrabKey` and the Wayland `GlobalShortcuts` portal are implemented behind the
  unchanged `HotkeyManager` API. The portal creates one session, batches `BindShortcuts`, reports
  asynchronous bind failures through `registrationFailed`, keeps suspend logical, and returns an empty
  `hotkey()` after the desktop assigns triggers. `Qt6::DBus` is Linux-only in `qtranslate` and the
  tests; `libqt6dbus6` is the runtime dependency for packaging and `dbus`/`dbus-daemon` the test
  dependency. Linux has 28 tests; `tst_hotkeymanager_x11` is 16/16 and `tst_hotkeymanager_wayland`
  20/20 in trixie and on Astra (after `fly-set-execid.sh`), `tst_autostart` is 6/6, and `tst_cli`
  is 20/20; `tst_secretprotector` is 7 passed + 1 skipped in trixie (no session keyring, recorded
  as not performed) and 8/8 on Astra with the real libsecret round-trip. Known gaps: the tray
  tooltip can show the configured sequence before an asynchronous bind completes or after a desktop
  reassignment (task 05/06, still open) and two latent signal-reentrancy edges with no current
  caller.
- X11 selection capture and the window helpers are implemented; Wayland reads the manually copied
  clipboard and never synthesizes input. `tst_selectiongrabber_linux` (12) and
  `tst_windowactivation_linux` (13) were added, and the manual `:0` check passed with `kate`
  (Ctrl+Q popup, Ctrl+Shift+Q replace, Ctrl+W main window, clipboard restore). The release-order
  defect was fixed in task 04 (`5b9f9fa`): the grabber waits for the activating key before Ctrl+C, so
  releasing Ctrl/Shift first, releasing Q first and holding the combination all work; the new
  `tst_hotkey_selection_x11` is 7/7 and is listed in `trusted.d`. The original clipboard's non-text
  formats are not restored by the manual check.
- CLI and desktop behaviour (task 06, `760ae0b`): `--translate-popup`, `--translate-main`,
  `--translate-inplace` are parsed by `src/app/commandline.*` and forwarded over the
  `SingleInstance` socket (`activate`/`noop` byte-exact); they combine with `--minimized` and
  `--verbose`, positional arguments are ignored, and unknown options exit 1. A command start does
  not show the main window before the grab; with a tray it stays in the tray, and without a tray
  the popup, main window, or in-place feedback appears as the scenario allows. One
  `Limited Wayland support` warning is shown per start and per-hotkey tray messages are suppressed
  on Wayland; a normal start without a tray shows one `System tray unavailable` warning, and
  in-place messages use a non-modal message box. `tst_cli` covers the parser, two-process
  forwarding and the legacy wire (20/20). The `.desktop` file, hicolor icons and
  `share/applications` are task 07.
- Roadmap stage 2 continues with packaging: task 07 is a stage with six children (desktop/install,
  tar.gz, `.deb`, AppImage, native Astra artifacts, CI); child 01 is done and child 02 is next.
  Task 08 is the final Linux gate. The READMEs and `architecture.md` still need the Linux install
  and limitation updates at task 08.

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
- All `.ts` files are in `i18n/`, 253 messages each, included in `QTRANSLATE_TS_FILES` in
  `CMakeLists.txt`; `.qm` files are embedded into `:/i18n`.
- `src/core/uilanguages.*`: codes, native names, resources; `normalize()` understands `system`,
  regional tags, `pt-PT` → `pt-BR`, `zh-CN`/`zh-Hans` → `zh-Hans`, `zh-Hant` → English;
  `canonical()` keeps the aliases (`de-DE` → `de`, `pt-PT` → `pt-BR`) in settings.
- `SettingsDialog` shows 15 languages; `AppController::applyUiLanguage()` loads the needed
  `QTranslator`, enables RTL for Arabic and sends `LanguageChange` without a restart.
- `LanguageComboBox` was switched to `AdjustToContents`, otherwise the German
  "Automatische Erkennung" was cut off.
- Added `tst_translations` (completeness of all `.ts`, `%1`/`%2`, emoji `⧉ ⇄ ✓ →`, leading and
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

- [`free-endpoints.md`](free-endpoints.md): DeepL web is implemented; Yandex web, Bing web, Reverso,
  Mozhi, MyMemory — research.
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
  Release: 23/23 tests.
- Live smoke through the built `DeepLWebService`: `Hello, world` → `Hallo, Welt`,
  `detected_source_language=en`, no errors.
- Yandex web: `api.browser.yandex.com/instaserp/translate`, form-urlencoded; Bing web:
  `www.bing.com/ttranslatev3` with tokens from HTML; Reverso: `cps.reverso.net/api2/TranslateText`;
  Mozhi: `/api/translate` on a public/own instance; MyMemory: `api.mymemory.translated.net/get`,
  ~500 bytes per request, the source language is required.
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
   and the documentation migration/Linux work is not included.
2. `a0f7d5e` bumped the version in `CMakeLists.txt` and `resources/qtranslate.rc`, added
   `doc/releases/0.11.1.md` (fullscreen hotkey pause, the manual **Pause hotkeys** tray item,
   `hotkeys/pauseInFullscreen`, no Linux claims) and updated `README.md`/`README.ru.md`.
3. Local Windows Debug/Release, `package.ps1` and installer checks were not performed (no MSVC);
   the tag CI was the build gate.
4. The branch and the annotated tag `v0.11.1` were pushed; CI succeeded and published the installer,
   portable ZIP and `SHA256SUMS.txt` (unlisted). No old tag was pushed or recreated.
5. Release: https://github.com/YoukaiCat/QTranslate/releases/tag/v0.11.1

## Next release

**0.12.0 after the Linux stage**, from `main`, with the version and scope agreed again. It follows
the same release flow: version bump, `doc/releases/0.12.0.md`, README links, local checks where
possible, tag CI as the build gate, and an explicit user approval before the push. Do not reuse the
`release/0.11.1` branch.

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

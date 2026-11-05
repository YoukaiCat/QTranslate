# Handoff: состояние работы

Дата: 2026-11-05. Репозиторий: `C:\projects\qtranslate`, ветка `main`.

## Git

- `origin/main` = `1e3e6b5` «Keep SHA256SUMS as an unlisted release asset»; тег `v0.10.0`
  указывает на `726dac0`.
- История: релиз 0.10.0, удаление checksum-ссылок и возврат `SHA256SUMS.txt` как unlisted asset.
- Текущая работа — OpenAI-совместимый перевод (OpenRouter): локальный коммит `bb71456`
  «Add OpenAI-compatible translation via OpenRouter», **не запушен**. Push — только по просьбе
  пользователя или при выпуске согласованного релиза.
- Рабочее дерево после завершения должно быть чистым; процесс `qtranslate` не запущен.

## Релизы

- `v0.9.0` — первый публичный релиз, тег и ассеты не трогать.
- `v0.9.1` — опубликован: https://github.com/youkaicat/qtranslate/releases/tag/v0.9.1
  - Установщик ~16,4 МБ, ZIP ~22,5 МБ, `SHA256SUMS.txt`; хэши проверены.
  - Установщик уже с галочками: меню «Пуск» (вкл.), рабочий стол (выкл.), автозагрузка (вкл.).
  - Успешный CI-прогон тега: https://github.com/youkaicat/qtranslate/actions/runs/41908976474
- `v0.9.1` собран из `00f342a`, **до** оптимизации размера и локализации. Не заменять его
  ассеты и не переставлять тег.
- `v0.10.0` — опубликован: https://github.com/youkaicat/qtranslate/releases/tag/v0.10.0
  - DeepL free web, 15 языков интерфейса, slim-дистрибутив.
  - Локально: Debug/Release 21/21, setup 11,51 МБ, ZIP 14,58 МБ.
  - CI: setup 10,63 МБ, ZIP 13,12 МБ; установка/запуск/удаление проверены.
  - `SHA256SUMS.txt` остаётся отдельным ассетом релиза; ссылка на него убрана из README/Downloads.
  - CI-прогон тега: https://github.com/youkaicat/qtranslate/actions/runs/41930568365
  - Тег и ассеты `v0.9.0`/`v0.9.1` не изменялись.

## Локализация (этап 12)

- Интерфейс: английский (исходный), русский + испанский, немецкий, французский, португальский
  (Бразилия), итальянский, польский, украинский, турецкий, китайский (упрощённый), японский,
  корейский, арабский, хинди — всего 15 языков.
- Все `.ts` лежат в `i18n/`, по 253 сообщения, включены в `QTRANSLATE_TS_FILES` в
  `CMakeLists.txt`; `.qm` встраиваются в `:/i18n`.
- `src/core/uilanguages.*`: коды, родные названия, ресурсы; `normalize()` понимает `system`,
  региональные теги, `pt-PT` → `pt-BR`, `zh-CN`/`zh-Hans` → `zh-Hans`, `zh-Hant` → английский;
  `canonical()` сохраняет алиасы (`de-DE` → `de`, `pt-PT` → `pt-BR`) в настройках.
- `SettingsDialog` показывает 15 языков; `AppController::applyUiLanguage()` загружает нужный
  `QTranslator`, для арабского включает RTL и рассылает `LanguageChange` без перезапуска.
- `LanguageComboBox` переведён на `AdjustToContents`, иначе немецкое «Automatische Erkennung»
  обрезалось.
- Добавлены `tst_translations` (полнота всех `.ts`, `%1`/`%2`, эмодзи `⧉ ⇄ ✓ →`, краевые
  пробелы) и `tst_uilanguages` (нормализация кодов и ресурсы).
- Debug и Release: 19/19 тестов на момент локализации; после DeepL free web — 21/21.
  Проверен запуск с `uiLanguage=es`, `de`, `ar`: переводы загружаются, арабский интерфейс
  зеркалируется, длинные немецкие подписи не обрезаются.
- Переводы — машинно-ассистированный первый проход. Перед публичным релизом желательна
  выборочная проверка носителями, особенно арабский RTL и длинные строки в настройках.
- `i18n/README.md` описывает добавление языка, `lupdate` и правила перевода.

## Подпись кода и UI Automation

- `doc/code-signing.md`: варианты подписи (самоподписанный, SignPath Foundation, Azure Artifact
  Signing, OV/EV, GitHub attestations), требования SignPath, что подписывать, Inno Setup,
  интеграция в CI. Текущие релизы **не подписаны**; SignPath требует OSI-лицензию, которой
  пока нет.
- `doc/architecture.md`, §4.4: подробности UI Automation — `TextPattern::GetSelection`, цепочка
  «UIA → буфер обмена», ограничение UIPI/UIAccess; §9.2 — словарные статьи `dt=bd` (части речи,
  значения, примеры; только для коротких текстов).

## Бесплатные веб-эндпоинты (этап 13, частично)

- `doc/free-endpoints.md`: DeepL web реализован; Yandex web, Bing web, Reverso, Mozhi, MyMemory —
  исследование.
- **DeepL web реализован:** `src/services/deeplwebservice.*` и `deeplwebparser.*`, сервис
  `deepl-web` с именем «DeepL (free web)». Ключ не нужен; доступен в списке сервисов сразу, как
  бесплатный Google. Скрытого переключателя нет; в настройках — предупреждение, что эндпоинт
  неофициальный.
- `POST https://oneshot-free.www.deepl.com/v1/translate`, `Authorization: None`, JSON;
  5000 символов на часть, запросы сериализуются и идут не чаще раза в 2 с, на `429` — одна
  повторная попытка через 3 с. `403` → `RateLimited`, `413` → `TooLong`, `5xx` → `Network`.
- Коды языков: базовые в нижнем регистре (`DE` → `de`), региональные — `pt-BR`, `en-US`,
  `zh-Hans`. Верхний регистр базового кода эндпоинт молча возвращает без перевода, поэтому
  приведение обязательно. `DE`, `PT-BR`, `EN-US`, `zh-Hans` проверены живыми запросами.
- `tst_deeplwebparser` и `tst_deeplwebservice`; Debug и Release: 21/21 тестов.
- Живой smoke через собранный `DeepLWebService`: `Hello, world` → `Hallo, Welt`,
  `detected_source_language=en`, ошибок нет.
- Yandex web: `api.browser.yandex.com/instaserp/translate`, form-urlencoded; Bing web:
  `www.bing.com/ttranslatev3` с токенами из HTML; Reverso: `cps.reverso.net/api2/TranslateText`;
  Mozhi: `/api/translate` на публичном/своём инстансе; MyMemory: `api.mymemory.translated.net/get`,
  ~500 байт на запрос, нужен исходный язык.
- Остальные, кроме MyMemory и self-hosted Mozhi/LibreTranslate, — неофициальные: нет SLA,
  возможны `429`, смена HTML/API и ограничения условий. Для них нужен rate limit и fallback.
- **DeepL API Developer** (ранее Free): 1 000 000 символов **всего**, без месячного сброса;
  после лимита — Growth/Pro. Прежняя информация про 500 000 в месяц устарела.

## Локальный offline-перевод (этап 14, вариант)

- `doc/local-translation.md`: вариант Argos-моделей + CTranslate2 без Python. Код **не реализован**;
  этап 14 в плане.
- Argos Translate — Python-библиотека, `pip install argostranslate` / `argospm install`; модели
  `.argosmodel` — zip с CTranslate2-моделью, `sentencepiece.model` и `metadata.json`. Подходит для
  прототипа, но не для поставки: нужен Python + spaCy/Stanza.
- Для продукта предпочтителен native-путь CTranslate2 с моделями `.argosmodel`; пример — MetalTranslate
  (MIT, экспериментальный). Нужны менеджер моделей, SHA-256, безопасная распаковка, сегментация
  предложений, pivot через английский и fallback на облако.
- Ограничения: качество ниже DeepL/Google, индекс Argos в основном `X ↔ en`, размер моделей
  десятки–сотни МБ, лицензии моделей проверять отдельно. NLLB/Seamless — CC-BY-NC, не для
  коммерческого распространения без проверки.
- ONNX — формат, не движок; нужен ONNX Runtime, для LLM — GenAI. Работает на CPU и Linux;
  DirectML только Windows. ONNX GenAI удобен при DirectML/OpenVINO/готовой ONNX-модели, но для
  локальной LLM проще llama.cpp + GGUF: готовых моделей больше, один файл, проще сборка.
  Гигабайты — это модель, а не движок; у обоих вариантов 4B/7B занимают 2,5–5 ГБ.
- Следующий шаг: PoC на Python-Argos (en↔ru, en↔de, pivot ru→de), замер качества/скорости/RAM;
  если приемлемо — native CTranslate2 runner.

## Проверка текста (этап 15, запланирован)

- `doc/proofreading.md`: отдельный режим проверки орфографии, грамматики и пунктуации; код не
  реализован.
- Отдельная вкладка настроек «Проверка текста» и независимые ключи `proofreading/*`; проверка не
  должна быть частью `TranslationService`.
- Провайдеры: LanguageTool (облако/self-hosted, грамматика/пунктуация), Hunspell/Nuspell
  (орфография), Yandex Speller (орфография ru/uk/en), LLM — опционально через diff.
- `ProofreadingIssue` с UTF-16 `offset`/`length`, `replacements`, `ruleId`, `category`,
  `severity`; замена в главном окне с undo, во внешнем приложении — с подтверждением.
- `checkBeforeTranslation` и `autoReplace` по умолчанию `false`; текст проверки не логируется.

## LLM-действия (этап 16, частично реализован)

- **Перевод через OpenAI-совместимый провайдер реализован:** сервис `llm`, класс
  `OpenAiCompatibleService`, транспорт `src/ai/openaichat.*`. OpenRouter — адрес по умолчанию;
  в настройках задаются `baseUrl`, `apiKey` (DPAPI), `model`, есть загрузка моделей.
- Имя сервиса вычисляется по хосту: `openrouter.ai` → «OpenRouter», `api.openai.com` → «OpenAI»,
  локальный адрес → «Local LLM», иначе хост. Это один адаптер, а не класс на каждого провайдера.
- Промпт: системная роль переводчика, текст как данные, JSON
  `{"detected_language","translation"}` для auto; Markdown-обёртки и обычный текст — fallback.
- Ошибки 401/403/402/429/413/5xx и таймаут маппятся в `TranslationError`; текст/ключи не логируются.
- `settings.llm` в INI — `[llm]`; смена модели/base URL очищает кэш переводов.
- Тесты: `tst_openaichat`, `tst_openaicompatibleservice`; Debug и Release — 23/23, `/W4` без
  предупреждений.
- Живой smoke OpenRouter выполнен через SOCKS5-прокси `127.0.0.1:2080`: `Hello, world` →
  `Привет, мир`, HTTP 200 за 6,6 с, модель `openrouter/free` (tokens 125 + 301). Прокси-тест в
  приложении исправлен: старый URL `translate.googleapis.com` отдавал 404 и давал ложную ошибку;
  теперь `https://www.gstatic.com/generate_204`, а любой HTTP-ответ кроме 407 считается успехом.
- `doc/llm-actions.md` обновлён: перевод считается готовым, остальные действия запланированы.
- Словарь вынесен в отдельный дизайн [`dictionary.md`](dictionary.md) и этап 17: LLM,
  Google `dt=bd`, CSV-глоссарий, Wiktionary; `DictionaryService`, не `TranslationService`.
- `LlmProvider`/`AiAction` для будущих действий: суммаризация, рерайтинг, объяснение,
  произношение, проверка; результат — предпросмотр/diff, замена только по подтверждению.
- LLM не заменяет DeepL/Google/LanguageTool по умолчанию. ASR/TTS/OCR — отдельные будущие задачи.

## Размер дистрибутива

Локальная сборка 0.10.0 (Qt 6.12):

- установленный `build/dist` — 35,0 МБ;
- `qtranslate-0.10.0-win64-setup.exe` — 11,51 МБ;
- `qtranslate-0.10.0-win64.zip` — 14,58 МБ;
- в ZIP 41 запись, 13 Qt-переводов; `opengl32sw.dll` отсутствует.

CI-сборка 0.10.0 (Qt 6.8.3 LTS):

- `qtranslate-0.10.0-win64-setup.exe` — 10,63 МБ;
- `qtranslate-0.10.0-win64.zip` — 13,12 МБ;
- установщик, ZIP и `SHA256SUMS.txt` проверены; файл остаётся unlisted asset.

Qt-переводы для 13 языков добавляют ~2,4 МБ в dist, ~0,32 МБ в установщик и ~0,68 МБ в ZIP.
`qt_de.qm` загружается из `build/dist/translations` без Qt в `PATH`; в логе —
`Loaded Qt translation "qt_de.qm" from ...`. Для хинди Qt 6.12 не содержит `qt_hi.qm`, поэтому
стандартные диалоги остаются английскими.

Что убрано:

- `opengl32sw.dll` (~19,7 МБ) — приложение не использует OpenGL/Qt Quick/QGraphicsView;
  Qt Widgets работает через raster. Запуск без библиотеки проверен;
- все переводы Qt, кроме `en/ru` (было 33 `.qm`, 5,7 МБ) — на момент `99ffdf4`. В локализации
  возвращены Qt-переводы для `ru,es,de,fr,pt_BR,it,pl,uk,tr,zh_CN,ja,ko,ar` (хинди в Qt 6.12 нет).

Отладочных PDB/debug-DLL в дистрибутиве нет; Qt-библиотеки Release.

**Важно:** локальные артефакты `0.10.0` собраны для проверки; опубликованы CI-ассеты,
собранные из тега. Локальные файлы вручную не загружать.

## Релиз 0.10.0 (2026-11-05)

1. Версия поднята в `CMakeLists.txt` и `resources/qtranslate.rc`; README, `doc/releases/0.10.0.md`,
   `doc/plan.md` и `doc/architecture.md` обновлены.
2. Debug и Release: 21/21, `/W4` без предупреждений.
3. `package.ps1` собрал setup 11,51 МБ, ZIP 14,58 МБ и `SHA256SUMS.txt`; файлы проверены.
4. Установщик проверен: тихая установка, `FileVersion`/`ProductVersion` 0.10.0, запуск без Qt
   в `PATH`, удаление; настройки и `HKCU\Run` восстановлены.
5. `main` и annotated tag `v0.10.0` запушены; CI-прогон успешен, ассеты скачаны и проверены.
6. Релиз: https://github.com/youkaicat/qtranslate/releases/tag/v0.10.0

## Следующий релиз (когда пользователь согласует)

1. ~~Ввести ключ OpenRouter и прокси `127.0.0.1:2080`, выполнить живой smoke~~ — выполнено:
   загрузка моделей и перевод `Hello, world` → `Привет, мир` через SOCKS5.
2. Желательно выборочно проверить переводы носителями (особенно арабский RTL и длинные строки).
3. Поднять версию, например до `0.11.0` (новая возможность): `CMakeLists.txt`
   (`project(VERSION ...)`) и `resources/qtranslate.rc` (`FileVersion`/`ProductVersion`).
   Версию и масштаб релиза согласовать с пользователем заранее.
4. Обновить под версию: `README.md`, `README.ru.md` (статус, имена/ссылки файлов),
   `doc/releases/<version>.md`, `doc/plan.md`, `doc/architecture.md`.
5. Собрать и проверить:
   - `cmd /c build\cli-build.cmd Debug`
   - `cmd /c build\cli-build.cmd Release`
   - `packaging/windows/package.ps1 -BuildDir build\cli-Release`
6. Проверить slim-установщик: запуск без `opengl32sw.dll`, галочки/autostart/Run, удаление,
   переключение языков, ключ OpenRouter и прокси. `SHA256SUMS.txt` оставить ассетом, но не
   добавлять в README/Downloads.
7. **Спросить пользователя и получить явное согласие на выпуск и push.**
8. После согласия: push `main`, новый annotated tag `vX.Y.Z`, push tag; CI опубликует ассеты.
   Старые теги/релизы не трогать.

## Правила пользователя

- Не пушить каждый коммит; GitHub Actions нагружается. Пушить только по просьбе или при
  выпуске согласованного релиза.
- Выпуск релиза и версию согласовывать заранее.
- Старые теги и релизы не изменять; исправления идут в новый релиз.
- README: Main features до скриншотов; точные имена файлов должны быть ссылками; стиль краткий.
- Не указывать `SHA256SUMS.txt` и другие checksum-файлы в README/Downloads; сам файл оставлять
  в ассетах релиза для тех, кому нужны хэши.

## Команды и особенности

- Быстрая локальная сборка: `cmd /c build\cli-build.cmd Debug` / `Release`. Скрипт
  `build/cli-build.cmd` не в git и жёстко зашит на `C:\projects\qtranslate`, Qt 6.12 и VS 2022.
  В worktree его не использовать.
- Голый `build\cli-Release\qtranslate.exe` не запускается двойным кликом без Qt в `PATH`
  (Entry Point Not Found). Для ручного запуска использовать `build\dist\bin\qtranslate.exe`
  после `package.ps1` или добавить `C:\Qt\6.12.0\msvc2022_64\bin` в `PATH`.
- Один тест: `ctest --test-dir build/cli-Release -R tst_name --output-on-failure`.
- CI использует Qt 6.8.3 LTS, потому что aqtinstall 3.3.0 не ставит Qt 6.12.
- `packaging/windows/qtranslate.iss` — UTF-8 с BOM; не терять BOM.
- В Inno `GetIniBool` не разобрал `false`; используется `GetIniString`. Autostart-задача ищется
  по caption (`FindAutostartItemIndex`), а не по имени задачи в `TasksList.Items`.
- Тесты установщика пишут в реальные `%APPDATA%`, HKCU Run и меню «Пуск»; делать backup/restore.
  Перед установкой/пересборкой останавливать qtranslate.
- Бесплатный Google требует HTTP/1.1 (`Http2AllowedAttribute=false`), иначе систематический 429.
- Секреты — DPAPI; в логах не должно быть текстов/ключей/паролей.

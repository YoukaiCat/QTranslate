# Handoff: состояние работы

Дата: 2026-11-05. Репозиторий: `C:\projects\qtranslate`, ветка `main`.

## Git

- `origin/main` = `8322b06` «Add AGENTS.md with project-specific guidance».
- Локально не запушены:
  - `676d918` — правила Git/релизов + прямые ссылки на файлы в README;
  - `99ffdf4` — уменьшение дистрибутива (без `opengl32sw.dll`, только `en/ru` переводы Qt);
  в `7a26d4d` Qt-переводы возвращены для поддерживаемых языков;
  - `aaa340a` — первый handoff;
  - `7a26d4d` — локализация на 13 языков, `doc/code-signing.md`, UI Automation в архитектуре;
  - `cd8d20a` — нормализация алиасов языка (`de-DE`, `pt-PT`) перед сохранением настроек;
  - `7ac5678` — возврат Qt-переводов для стандартных диалогов и обновление размеров;
  - `80feeb5` — исследование бесплатных веб-эндпоинтов и лимит DeepL Developer;
  - коммит с этим handoff.
- Рабочее дерево после коммита handoff должно быть чистым; процесс `qtranslate` не запущен.

## Релизы

- `v0.9.0` — первый публичный релиз, тег и ассеты не трогать.
- `v0.9.1` — опубликован: https://github.com/youkaicat/qtranslate/releases/tag/v0.9.1
  - Установщик ~16,4 МБ, ZIP ~22,5 МБ, `SHA256SUMS.txt`; хэши проверены.
  - Установщик уже с галочками: меню «Пуск» (вкл.), рабочий стол (выкл.), автозагрузка (вкл.).
  - Успешный CI-прогон тега: https://github.com/youkaicat/qtranslate/actions/runs/41908976474
- `v0.9.1` собран из `00f342a`, **до** оптимизации размера и локализации. Не заменять его
  ассеты и не переставлять тег.

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
- Debug и Release: 19/19 тестов. Проверен запуск с `uiLanguage=es`, `de`, `ar`: переводы
  загружаются, арабский интерфейс зеркалируется, длинные немецкие подписи не обрезаются.
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

## Бесплатные веб-эндпоинты (этап 13)

- `doc/free-endpoints.md`: исследование бесплатных эндпоинтов без ключа — DeepL web, Yandex web,
  Bing web, Reverso, Mozhi, MyMemory. Код **не реализован**; этап 13 в плане.
- DeepL web: `POST https://oneshot-free.www.deepl.com/v1/translate`, `Authorization: None`,
  JSON; QTranslate использует его как fallback, 2 с между запросами, 5000 символов, `429`.
  Endpoint проверен 2026-11-05: первый запрос дал `429`, второй — `200`.
- Yandex web: `api.browser.yandex.com/instaserp/translate`, form-urlencoded; Bing web:
  `www.bing.com/ttranslatev3` с токенами из HTML; Reverso: `cps.reverso.net/api2/TranslateText`;
  Mozhi: `/api/translate` на публичном/своём инстансе; MyMemory: `api.mymemory.translated.net/get`,
  ~500 байт на запрос, нужен исходный язык.
- Все, кроме MyMemory и self-hosted Mozhi/LibreTranslate, — неофициальные: нет SLA, возможны
  `429`, смена HTML/API и ограничения условий. Включать только как экспериментальные, выключенные
  по умолчанию, с rate limit и fallback.
- **DeepL API Developer** (ранее Free): 1 000 000 символов **всего**, без месячного сброса;
  после лимита — Growth/Pro. Прежняя информация про 500 000 в месяц устарела.

## Размер дистрибутива

Локальная сборка после локализации (Qt 6.12):

- установленный `build/dist` — 35,0 МБ (32,6 МБ без Qt-переводов);
- `qtranslate-0.9.1-win64-setup.exe` — 11,50 МБ (11,18 МБ);
- `qtranslate-0.9.1-win64.zip` — 14,11 МБ (13,43 МБ).

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

**Важно:** локальные slim-артефакты называются `0.9.1`. Не загружать их в релиз `v0.9.1`;
они предназначены для следующего согласованного релиза.

## Следующий релиз (когда пользователь согласует)

1. Желательно выборочно проверить переводы носителями (особенно арабский RTL и длинные строки).
2. Поднять версию, например до `0.9.2`: `CMakeLists.txt` (`project(VERSION ...)`) и
   `resources/qtranslate.rc` (`FileVersion`/`ProductVersion`).
3. Обновить под версию: `README.md`, `README.ru.md` (статус, имена/ссылки файлов),
   `doc/releases/<version>.md`, `doc/plan.md`, `doc/architecture.md`.
4. Собрать и проверить:
   - `cmd /c build\cli-build.cmd Debug`
   - `cmd /c build\cli-build.cmd Release`
   - `packaging/windows/package.ps1 -BuildDir build\cli-Release`
5. Проверить slim-установщик: запуск без `opengl32sw.dll`, галочки/autostart/Run, удаление,
   `SHA256SUMS.txt`, переключение языков.
6. **Спросить пользователя и получить явное согласие на выпуск и push.**
7. После согласия: push `main`, новый annotated tag `vX.Y.Z`, push tag; CI опубликует ассеты.
   Старые теги/релизы не трогать.

## Правила пользователя

- Не пушить каждый коммит; GitHub Actions нагружается. Пушить только по просьбе или при
  выпуске согласованного релиза.
- Выпуск релиза и версию согласовывать заранее.
- Старые теги и релизы не изменять; исправления идут в новый релиз.
- README: Main features до скриншотов; точные имена файлов должны быть ссылками; стиль краткий.

## Команды и особенности

- Быстрая локальная сборка: `cmd /c build\cli-build.cmd Debug` / `Release`. Скрипт
  `build/cli-build.cmd` не в git и жёстко зашит на `C:\projects\qtranslate`, Qt 6.12 и VS 2022.
  В worktree его не использовать.
- Один тест: `ctest --test-dir build/cli-Release -R tst_name --output-on-failure`.
- CI использует Qt 6.8.3 LTS, потому что aqtinstall 3.3.0 не ставит Qt 6.12.
- `packaging/windows/qtranslate.iss` — UTF-8 с BOM; не терять BOM.
- В Inno `GetIniBool` не разобрал `false`; используется `GetIniString`. Autostart-задача ищется
  по caption (`FindAutostartItemIndex`), а не по имени задачи в `TasksList.Items`.
- Тесты установщика пишут в реальные `%APPDATA%`, HKCU Run и меню «Пуск»; делать backup/restore.
  Перед установкой/пересборкой останавливать qtranslate.
- Бесплатный Google требует HTTP/1.1 (`Http2AllowedAttribute=false`), иначе систематический 429.
- Секреты — DPAPI; в логах не должно быть текстов/ключей/паролей.

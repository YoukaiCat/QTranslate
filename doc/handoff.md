# Handoff: состояние работы

Дата: 2026-11-05. Репозиторий: `C:\projects\qtranslate`, ветка `main`.

## Git

- `origin/main` = `8322b06` «Add AGENTS.md with project-specific guidance».
- Локально не запушены:
  - `676d918` — правила Git/релизов + прямые ссылки на файлы в README;
  - `99ffdf4` — уменьшение дистрибутива (без `opengl32sw.dll`, только `en/ru` переводы Qt);
  - `aaa340a` — первый handoff;
  - `7a26d4d` — локализация на 13 языков, `doc/code-signing.md`, UI Automation в архитектуре;
  - `cd8d20a` — нормализация алиасов языка (`de-DE`, `pt-PT`) перед сохранением настроек;
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

## Размер дистрибутива

Локальная сборка после `99ffdf4` (Qt 6.12), без учёта встроенных `.qm`:

- установленный `build/dist` — 32,6 МБ (было 57,8 МБ);
- `qtranslate-0.9.1-win64-setup.exe` — 11,18 МБ (было 16,6 МБ);
- `qtranslate-0.9.1-win64.zip` — 13,43 МБ (было 22,4 МБ).

Что убрано:

- `opengl32sw.dll` (~19,7 МБ) — приложение не использует OpenGL/Qt Quick/QGraphicsView;
  Qt Widgets работает через raster. Запуск без библиотеки проверен;
- все переводы Qt, кроме `en/ru` (было 33 `.qm`, 5,7 МБ).

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

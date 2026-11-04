# Handoff: состояние работы

Дата: 2026-11-04. Репозиторий: `C:\projects\qtranslate`, ветка `main`.

## Git

- Состояние кода до добавления этого handoff: `99ffdf4` «Slim the distribution by dropping
  software OpenGL and extra Qt translations».
- `origin/main` = `8322b06` «Add AGENTS.md with project-specific guidance».
- Локально не запушены: `676d918` (правила Git/релизов + прямые ссылки в README), `99ffdf4`
  (уменьшение дистрибутива) и коммит с этим `doc/handoff.md`.
- Рабочее дерево после коммита handoff должно быть чистым; процесс `qtranslate` не запущен.

## Релизы

- `v0.9.0` — первый публичный релиз, тег и ассеты не трогать.
- `v0.9.1` — опубликован: https://github.com/youkaicat/qtranslate/releases/tag/v0.9.1
  - Установщик ~16,4 МБ, ZIP ~22,5 МБ, `SHA256SUMS.txt`; хэши проверены.
  - Установщик уже с галочками: меню «Пуск» (вкл.), рабочий стол (выкл.), автозагрузка (вкл.).
  - Успешный CI-прогон тега: https://github.com/youkaicat/qtranslate/actions/runs/41908976474
- `v0.9.1` собран из `00f342a`, **до** оптимизации размера. Не заменять его ассеты и не
  переставлять тег.

## Размер дистрибутива

Локальная сборка после `99ffdf4` (Qt 6.12):

- установленный `build/dist` — 32,6 МБ (было 57,8 МБ);
- `qtranslate-0.9.1-win64-setup.exe` — 11,18 МБ (было 16,6 МБ);
- `qtranslate-0.9.1-win64.zip` — 13,43 МБ (было 22,4 МБ).

Что убрано:

- `opengl32sw.dll` (~19,7 МБ) — приложение не использует OpenGL/Qt Quick/QGraphicsView;
  Qt Widgets работает через raster. Запуск без библиотеки проверен;
- все переводы Qt, кроме `en/ru` (было 33 `.qm`, 5,7 МБ).

Отладочных PDB/debug-DLL в дистрибутиве нет; Qt-библиотеки Release.

**Важно:** локальные slim-артефакты сейчас называются `0.9.1`. Не загружать их в релиз
`v0.9.1`; они предназначены для следующего согласованного релиза.

## Что уже готово

- Установщик: Start menu/desktop/autostart, синхронизация `[app] autostart` с HKCU Run,
  удаление Run при деинсталляции, `AppMutex`.
- README.md и README.ru.md: краткий Main features, скриншоты, прямые ссылки на файлы v0.9.1
  (ссылки есть только в локальном коммите `676d918`, на GitHub ещё не отправлены).
- `AGENTS.md`: правила проекта, сборка/тесты, релизный процесс, gotchas установщика.
- `doc/plan.md`: этап 10 (0.9.1) завершён; этап 11 Linux и этап 12 локализация запланированы.
- `doc/architecture.md`: актуализированы установщик, CI, Linux-план, локализация, размеры.

## Следующий релиз (когда пользователь согласует)

1. Поднять версию, например до `0.9.2`: `CMakeLists.txt` (`project(VERSION ...)`) и
   `resources/qtranslate.rc` (`FileVersion`/`ProductVersion`).
2. Обновить под версию: `README.md`, `README.ru.md` (статус, имена/ссылки файлов),
   `doc/releases/<version>.md`, `doc/plan.md`, `doc/architecture.md`.
3. Собрать и проверить:
   - `cmd /c build\cli-build.cmd Debug`
   - `cmd /c build\cli-build.cmd Release`
   - `packaging/windows/package.ps1 -BuildDir build/cli-Release`
4. Проверить slim-установщик: запуск без `opengl32sw.dll`, галочки/autostart/Run, удаление,
   `SHA256SUMS.txt`.
5. **Спросить пользователя и получить явное согласие на выпуск и push.**
6. После согласия: push `main`, новый annotated tag `vX.Y.Z`, push tag; CI опубликует ассеты.
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

# Переводы интерфейса

Английский — исходный язык. Строки интерфейса помечены `tr()` или `QT_TRANSLATE_NOOP`; переводы
лежат в `*.ts` рядом с этим файлом и компилируются `qt_add_translations` в `.qm`, которые
встраиваются в ресурсы `:/i18n`.

## Поддерживаемые языки

| Код | Файл | Язык |
|---|---|---|
| `en` | — | English (исходный) |
| `ru` | `qtranslate_ru_RU.ts` | Русский |
| `es` | `qtranslate_es.ts` | Español |
| `de` | `qtranslate_de.ts` | Deutsch |
| `fr` | `qtranslate_fr.ts` | Français |
| `pt-BR` | `qtranslate_pt_BR.ts` | Português (Brasil) |
| `it` | `qtranslate_it.ts` | Italiano |
| `pl` | `qtranslate_pl.ts` | Polski |
| `uk` | `qtranslate_uk.ts` | Українська |
| `tr` | `qtranslate_tr.ts` | Türkçe |
| `zh-Hans` | `qtranslate_zh_CN.ts` | 简体中文 |
| `ja` | `qtranslate_ja.ts` | 日本語 |
| `ko` | `qtranslate_ko.ts` | 한국어 |
| `ar` | `qtranslate_ar.ts` | العربية |
| `hi` | `qtranslate_hi.ts` | हिन्दी |

Список кодов и родных названий для выпадающего списка задаётся в `src/core/uilanguages.cpp`.
Добавление языка требует новой строки в этой таблице, нового `.ts` и записи в
`QTRANSLATE_TS_FILES` в `CMakeLists.txt`.

Стандартные диалоги Qt (кнопки `QMessageBox`, контекстные меню, файловые диалоги) переводятся
отдельными `qt_<locale>.qm`. Они деплоятся через `--translations` в `CMakeLists.txt` и
загружаются в `AppController::applyUiLanguage()` из `translations/` рядом с установленным exe
(в dev-сборке — из `QLibraryInfo::TranslationsPath`). В Qt 6.12 нет `qt_hi.qm`, поэтому
стандартные диалоги для хинди остаются английскими.

## Обновление строк

После добавления или изменения `tr()`-строк:

```powershell
C:\Qt\6.12.0\msvc2022_64\bin\lupdate.exe src -ts i18n/qtranslate_ru_RU.ts i18n/qtranslate_es.ts ... --no-obsolete
```

Либо собрать цель `update_translations`, если она доступна в выбранном генераторе. Новые строки
появятся с `type="unfinished"`; их нужно перевести. `tst_translations` падает, если в любом
поддерживаемом `.ts` осталась пустая или незавершённая строка.

Проверка целостности:

```powershell
ctest --test-dir build/cli-Release -R tst_translations --output-on-failure
```

## Правила перевода

- Сохранять `%1`, `%2`, `%n` и другие placeholder'ы без изменений.
- Сохранять эмодзи и символы `📋`, `↗`, `✓`, `→`.
- Не переводить названия `qtranslate`, `DeepL`, `HTTP`, `SOCKS5`; `Google Translate` можно
  оставить брендом или использовать локальное название сервиса.
- Не добавлять и не удалять краевые пробелы у строк вида `" characters"` и `" ms"`.
- Для арабского проверять RTL: окна, меню, popup и настройки не должны обрезаться.
- Переводы — машинно-ассистированный первый проход; перед релизом желательна выборочная проверка
  носителями языка.

## Добавление нового языка

1. Создать `.ts` с `lupdate` или скопировать существующий и заменить `language`.
2. Добавить файл в `QTRANSLATE_TS_FILES` в `CMakeLists.txt`.
3. Добавить код, родное название и путь к `.qm` в `src/core/uilanguages.cpp`.
4. Добавить файл в список `tst_translations` и проверить нормализацию кодов в `tst_uilanguages`.
5. Собрать Debug/Release и проверить переключение языка без перезапуска.

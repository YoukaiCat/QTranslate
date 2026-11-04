# qtranslate

[![build](https://github.com/youkaicat/qtranslate/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/youkaicat/qtranslate/actions/workflows/build.yml)

Лёгкий десктопный переводчик. Живёт в системном трее, переводит выделенный текст в
любой программе по глобальной горячей клавише и умеет сразу заменять выделение переводом в
активном поле ввода.

> **Статус:** 0.9.0, release candidate. Доступны установщик для Windows и портативный архив.
> Поддержка Linux в разработке.

[English](README.md) | **Русский**

## Скриншоты

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="Главное окно переводчика" width="620">
</p>

<p align="center">
  <img src="docs/screenshots/popup-translation.png" alt="Перевод во всплывающем окне по Ctrl+Q" width="560">
</p>

<p align="center">
  <img src="docs/screenshots/settings-translation.png" alt="Настройки перевода" width="49%">
  <img src="docs/screenshots/settings-hotkeys.png" alt="Настройки горячих клавиш" width="49%">
</p>

## Основные возможности

- **Ctrl+Q** переводит выделенный текст в компактном окне рядом с курсором.
- **Ctrl+Shift+Q** переводит выделенный текст и заменяет им выделение в активном поле ввода —
  удобно для ответов в чатах.
- **Ctrl+W** открывает главное окно с переведённым выделением.
- Google Translate (бесплатный endpoint или Cloud API) и DeepL (Free/Pro).
- Автоопределение исходного языка, умное переключение на дополнительный язык, кэш переводов в
  памяти и автоперевод при наборе.
- Системный или ручной HTTP/SOCKS5-прокси с необязательной авторизацией.
- Русский и английский интерфейс, автозагрузка, портативный INI-режим, `--verbose`-логирование.

## Скачивание и установка

Последняя сборка — на странице [Releases](https://github.com/youkaicat/qtranslate/releases/latest):

- **Установщик** — `qtranslate-0.9.0-win64-setup.exe`. Устанавливает qtranslate для текущего
  пользователя без прав администратора, создаёт ярлыки в меню «Пуск» и, по желанию, на рабочем
  столе, добавляет деинсталлятор. При удалении настройки в `%APPDATA%\qtranslate` сохраняются.
- **Портативный архив** — `qtranslate-0.9.0-win64.zip`. Распакуйте и запустите
  `bin\qtranslate.exe`; установка и права администратора не нужны. Архив самодостаточен: Qt и
  runtime MSVC уже включены.
- **`SHA256SUMS.txt`** — контрольные суммы SHA-256 обоих файлов.

Установщик пока не подписан кодом, поэтому Windows SmartScreen может показать предупреждение.

## Использование

```
qtranslate [--minimized] [--verbose]
```

- `Ctrl+Q` — перевести выделение во всплывающем окне рядом с курсором.
- `Ctrl+Shift+Q` — перевести выделение и заменить им текст в активном поле ввода.
- `Ctrl+W` — открыть главное окно с переведённым выделением.
- `Ctrl+Enter` — перевести текст в главном окне.
- Настройки, выбор сервиса, «Показать окно» и «Выход» доступны в меню трея. Горячие клавиши,
  языки, прокси, автозагрузка и ключи API настраиваются в **Настройках**.
- `--minimized` запускает программу скрытой в трее; `--verbose` пишет подробный лог в
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`.
- Портативный режим: положите пустой `qtranslate.ini` рядом с `qtranslate.exe`, и программа будет
  использовать его вместо `%APPDATA%\qtranslate\qtranslate.ini`.

## Сборка

### Windows

Требования:

- Windows 10/11, 64-бит
- Qt 6.5 или новее (разработка ведётся на Qt 6.12) с MSVC 2022 x64
- CMake 3.19 или новее
- Visual Studio 2022 (MSVC)

Откройте `CMakeLists.txt` в Qt Creator или соберите из *x64 Native Tools Command Prompt*:

```bat
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

С CMake старше 3.22 каталог Qt `bin` должен быть в `PATH`, чтобы `ctest` нашёл DLL Qt. Тесты
собираются по умолчанию; `-DQTRANSLATE_BUILD_TESTS=OFF` отключает их.

Чтобы собрать артефакты релиза (портативный ZIP, установщик для текущего пользователя и
`SHA256SUMS.txt`), установите [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php) и выполните:

```powershell
./packaging/windows/package.ps1 -BuildDir build/release
```

Результат — в `build/artifacts`. Workflow CI
[`.github/workflows/build.yml`](.github/workflows/build.yml) запускает те же шаги на
`windows-latest` с Qt 6.8.3 LTS. Пуш тега вроде `v0.9.0` публикует GitHub Release с этими
файлами; версия тега должна совпадать с версией в `CMakeLists.txt`.

### Linux (в разработке)

Порт под Linux пока не завершён. Ядро, сетевой слой и интерфейс на Qt Widgets спроектированы
переносимыми, но платформенные модули сейчас — заглушки: глобальные горячие клавиши, захват
выделения, вставка в активное окно и автозагрузка не реализованы. Полноценный порт запланирован
на следующий релиз; CI пока собирает только Windows.

Чтобы собрать текущее дерево под Linux для разработки:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
```

## Известные ограничения

- Захват выделения не работает в окнах процессов, запущенных от имени администратора.
- Бесплатный Google может ответить `HTTP 429`, если ограничит частоту запросов; подождите или
  используйте Google Cloud/DeepL с ключом.
- Промежуточный `Ctrl+C` при захвате выделения может попасть в историю буфера обмена Windows
  (`Win+V`); исходное содержимое буфера восстанавливается.
- Установщик пока не подписан кодом.
- Поддержка Linux ещё не готова; см. раздел сборки.

Переводимый текст отправляется в выбранный онлайн-сервис (Google или DeepL). qtranslate не
записывает тексты в лог.

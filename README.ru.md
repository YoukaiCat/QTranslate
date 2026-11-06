# QTranslate

[![build](https://github.com/youkaicat/QTranslate/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/youkaicat/QTranslate/actions/workflows/build.yml)

Лёгкий десктопный переводчик. Живёт в системном трее, переводит выделенный текст в
любой программе по глобальной горячей клавише и умеет сразу заменять выделение переводом в
активном поле ввода.

> **Статус:** 0.11.0 выпущен. Доступны установщик для Windows и портативный архив.
> Что изменилось — в [заметках к релизу 0.11.0](doc/releases/0.11.0.md).
> Поддержка Linux в разработке.

[English](README.md) | **Русский**

## Основные возможности

- **Ctrl+Q** переводит выделенный текст в компактном окне рядом с курсором.
- **Ctrl+Shift+Q** переводит выделенный текст и заменяет им выделение в активном поле ввода —
  удобно для ответов в чатах.
- **Ctrl+W** открывает главное окно с переведённым выделением.
- Google Translate (бесплатный endpoint или Cloud API) и DeepL (API-ключ или неофициальный
  бесплатный веб-режим), а также OpenRouter или другой OpenAI-совместимый провайдер. У OpenRouter
  есть бесплатные модели, например `openrouter/free`; подойдёт любой совместимый endpoint, включая
  локальные Ollama, LM Studio или llama.cpp. Настройка — в разделе
  [«Провайдеры перевода»](#провайдеры-перевода).
- Автоопределение исходного языка, умное переключение на дополнительный язык, кэш переводов в
  памяти и автоперевод при наборе.
- Системный или ручной HTTP/SOCKS5-прокси с необязательной авторизацией.
- Интерфейс на 15 языках (русский, английский, испанский, немецкий, французский, португальский,
  итальянский, польский, украинский, турецкий, китайский, японский, корейский, арабский, хинди),
  автозагрузка и портативный INI-режим.

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

## Скачивание и установка

Последняя сборка — на странице [Releases](https://github.com/youkaicat/QTranslate/releases/latest):

- **[`qtranslate-0.11.0-win64-setup.exe`](https://github.com/youkaicat/QTranslate/releases/download/v0.11.0/qtranslate-0.11.0-win64-setup.exe)**. Устанавливает QTranslate для текущего
  пользователя без прав администратора; по умолчанию создаёт ярлык в меню «Пуск» и включает
  автозагрузку, ярлык на рабочем столе — по желанию. Добавляет деинсталлятор. При удалении
  настройки в `%APPDATA%\qtranslate` сохраняются.
- **[`qtranslate-0.11.0-win64.zip`](https://github.com/youkaicat/QTranslate/releases/download/v0.11.0/qtranslate-0.11.0-win64.zip)**. Распакуйте и запустите
  `bin\qtranslate.exe`; установка и права администратора не нужны. Архив самодостаточен: Qt и
  runtime MSVC уже включены.
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
- Пока на переднем плане полноэкранное приложение, глобальные горячие клавиши автоматически
  освобождаются, поэтому игры получают Ctrl+Q/Ctrl+W. Это можно выключить в
  **Настройки → Горячие клавиши**; **Трей → «Приостановить горячие клавиши»** отключает их вручную.
- `--minimized` запускает программу скрытой в трее; `--verbose` пишет подробный лог в
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`.
- Портативный режим: положите пустой `qtranslate.ini` рядом с `qtranslate.exe`, и программа будет
  использовать его вместо `%APPDATA%\qtranslate\qtranslate.ini`.

## Провайдеры перевода

- **Google Translate** — бесплатный публичный endpoint или Cloud Translation API с ключом. Режим
  выбирается в **Настройки → Сервисы → Google Translate**.
- **DeepL** — официальный API с ключом или неофициальный бесплатный веб-режим без ключа. Оба
  режима находятся в **Настройки → Сервисы → DeepL**.
- **OpenAI-совместимый провайдер** — по умолчанию используется [OpenRouter](https://openrouter.ai)
  (`https://openrouter.ai/api/v1`). OpenRouter **требует ключ**; у него есть бесплатные модели,
  например `openrouter/free`, и платные. Вставьте ключ, при необходимости измените модель и
  нажмите **Загрузить модели**.
  В то же поле адреса сервера можно вписать любой OpenAI-совместимый endpoint: OpenAI
  (`https://api.openai.com/v1`), TeamRouter, LM Studio, llama.cpp или локальный Ollama. Имя модели
  должно совпадать с тем, которое отдаёт этот сервер.

### Ollama (локально, без ключа)

1. Установите [Ollama](https://ollama.com) для Windows, macOS или Linux.
2. Запустите её и скачайте модель, например:
   ```bat
   ollama pull bonsai-2:latest
   ```
3. В QTranslate откройте **Настройки → Сервисы → Провайдер с поддержкой OpenAI** и укажите:
   - Адрес сервера: `http://localhost:11434/v1`
   - API-ключ: оставьте пустым
   - Модель: имя из `ollama list`, например `bonsai-2:latest`
4. Нажмите **Загрузить модели** или введите имя вручную, затем **OK**. В списке сервисов выберите
   **«Локальная LLM»**.

Ollama обычно слушает `http://localhost:11434`; если сервер не запущен, выполните `ollama serve`.
OpenAI-совместимый API находится по пути `/v1`.
Подробнее: [Ollama OpenAI compatibility](https://docs.ollama.com/api/openai-compatibility).

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

Чтобы собрать артефакты релиза (портативный ZIP и установщик для текущего пользователя),
установите [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php) и выполните:

```powershell
./packaging/windows/package.ps1 -BuildDir build/release
```

Результат — в `build/artifacts`. Workflow CI
[`.github/workflows/build.yml`](.github/workflows/build.yml) запускает те же шаги на
`windows-latest` с Qt 6.8.3 LTS. Пуш тега вроде `v0.11.0` публикует GitHub Release с этими
файлами; версия тега должна совпадать с версией в `CMakeLists.txt`.

### Linux (в разработке)

Порт под Linux пока не завершён. Дерево собирается, компонуется и проходит 20 из 20 тестов на
Debian 13 (trixie, Qt 6.8.2) и Astra Linux 1.8.7 (Qt 6.10.2) с `-Wall -Wextra` и без
предупреждений; платформенные модули пока — заглушки: глобальные горячие клавиши, захват
выделения, вставка в активное окно и автозагрузка не реализованы. CI пока собирает только
Windows.

Чтобы собрать текущее дерево под Linux для разработки (Qt 6.5+; пакеты ниже — для Debian 13 и
Astra):

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev \
  qt6-tools-dev-tools libx11-dev libxtst-dev libsecret-1-dev xvfb xauth
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
xvfb-run -a ctest --test-dir build --output-on-failure
```

Список пакетов совпадает с `packaging/linux/trixie.Dockerfile`; `xvfb-run` нужен, потому что
`tst_hotkeysequence` открывает окно, а у обычного `ctest` нет дисплея.

#### Astra Linux: глобальные горячие клавиши

Защита XPARSEC в X-сервере Astra Linux запрещает пассивные захваты `XGrabKey` непроверенным
бинарникам, поэтому в обычной установке пакета глобальные горячие клавиши не работают, пока
X-сервер не доверяет бинарнику. После установки QTranslate из пакета добавьте
`/usr/bin/qtranslate` в список доверенных клиентов от root:

```bash
sudo mkdir -p /etc/X11/trusted.d
echo '/usr/bin/qtranslate(KBD_R)' | sudo tee /etc/X11/trusted.d/qtranslate
sudo chown root:root /etc/X11/trusted.d/qtranslate
sudo chmod 644 /etc/X11/trusted.d/qtranslate
sudo /usr/bin/fly-set-execid.sh
getfattr -n security.exec_id /usr/bin/qtranslate   # из пакета attr; ожидается 0sEA== (0x10, KBD_R)
```

Перезапустите QTranslate; если X-сервер уже закэшировал список клиентов, перезайдите в сессию.

`KBD_R` разрешает процессу читать и захватывать весь ввод с клавиатуры — ровно то, что
ограничивает XPARSEC. Выдавайте её только root-owned `/usr/bin/qtranslate` из пакета и только по
согласованию с политикой безопасности. Чтобы отозвать, выполните
`sudo setfattr -x security.exec_id /usr/bin/qtranslate` и удалите
`/etc/X11/trusted.d/qtranslate`.

## Известные ограничения

- Захват выделения не работает в окнах процессов, запущенных от имени администратора.
- Бесплатный Google может ответить `HTTP 429`, если ограничит частоту запросов; подождите или
  используйте Google Cloud/DeepL с ключом.
- Режим DeepL free web — неофициальный эндпоинт без API-ключа. Он может ограничивать частоту,
  меняться или перестать работать; официальный DeepL API надёжнее.
- OpenRouter и другие OpenAI-совместимые провайдеры требуют API-ключ и доступную модель. Качество,
  скорость и стоимость зависят от выбранной модели; у бесплатных моделей жёсткие лимиты. Текст
  отправляется выбранному провайдеру.
- Промежуточный `Ctrl+C` при захвате выделения может попасть в историю буфера обмена Windows
  (`Win+V`); исходное содержимое буфера восстанавливается.
- Установщик пока не подписан кодом.
- Интерфейс на хинди переведён, но в Qt 6.12 нет каталога для хинди, поэтому стандартные диалоги
  Qt остаются на английском.
- Поддержка Linux ещё не готова; см. раздел сборки.

Переводимый текст отправляется выбранному провайдеру: Google, DeepL, OpenRouter или другому
OpenAI-совместимому серверу. Локальный сервер, например Ollama, оставляет текст на вашей машине.
QTranslate не записывает тексты в лог.

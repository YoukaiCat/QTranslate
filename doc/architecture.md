# qtranslate — архитектура

Документ описывает устройство десктопного переводчика qtranslate: из каких модулей он состоит,
как они связаны и как проходят основные сценарии. Это опорный документ для реализации: если код
расходится с ним, нужно либо исправить код, либо осознанно обновить документ.

---

## 1. Цели и требования

### 1.1. Функциональные требования

| # | Требование |
|---|------------|
| F1 | Запуск свёрнутым в системный трей; закрытие окна прячет его в трей, а не завершает программу. |
| F2 | Автозагрузка вместе с Windows (включается в настройках). При автозапуске программа стартует сразу в трее. |
| F3 | Перевод выделенного в любой программе текста по глобальной горячей клавише. |
| F4 | **Ctrl+Q** — перевести выделенный текст во всплывающем окне рядом с курсором. |
| F5 | **Ctrl+Shift+Q** — перевести выделенный текст и заменить им выделение в активном поле ввода (без всплывающего окна и ручного копирования). |
| F6 | **Ctrl+W** — перевести выделенный текст в главном окне программы (окно открывается и активируется). |
| F7 | Горячие клавиши настраиваются; Ctrl+Q / Ctrl+Shift+Q / Ctrl+W — значения по умолчанию. |
| F8 | Сервисы перевода: Google Translate и DeepL; активный сервис выбирается в настройках и в трее. |
| F9 | Сетевые запросы можно пускать через прокси: HTTP (в том числе для https-запросов, через `CONNECT`), SOCKS5 (с авторизацией и без), либо системный прокси. |

### 1.2. Нефункциональные требования

- **Быстро.** Холодный старт меньше 300 мс до появления значка в трее. От нажатия горячей клавиши до
  появления всплывающего окна со «спиннером» — не больше 150 мс; дальше время определяется сетью.
- **Легко.** Один процесс, один поток GUI, без фоновых служб. В простое ~0% CPU и меньше 30 МБ памяти.
- **Не мешать.** Программа не должна портить буфер обмена пользователя и не должна красть фокус без
  явного действия пользователя.
- **Расширяемо.** Новый сервис перевода добавляется одним классом без правки остального кода.
- **Тестируемо.** Логика без UI и WinAPI (разбор ответов сервисов, языки, разбиение текста, настройки)
  покрыта модульными тестами.

### 1.3. Технологический стек

- C++17, Qt 6.5+ (Windows: Qt 6.12, MSVC 2022 x64; Linux: GCC/Clang — этап 11), CMake ≥ 3.19.
- Модули Qt: `Core`, `Gui`, `Widgets`, `Network`. В тестах дополнительно `Test`.
- Платформы: **Windows 10/11 x64 — текущая**; **Linux x86_64 — запланирована** (этап 11);
  macOS не планируется. Платформенный код изолирован (см. §3.3 и §4.14), чтобы порт не трогал ядро.
- TLS: на Windows — штатный Schannel-бэкенд Qt 6 (`qschannelbackend`), OpenSSL не нужен; на Linux
  будет использоваться OpenSSL-бэкенд Qt.
- WinAPI (только Windows): `user32` (горячие клавиши, `SendInput`, буфер обмена), `crypt32`
  (DPAPI для секретов).

### 1.4. Матрица платформ

| Компонент | Windows 10/11 x64 (0.9.0) | Linux x86_64 (план, этап 11) |
|---|---|---|
| Сборка | MSVC 2022, Qt 6.12 | GCC/Clang, Qt 6.5+, CMake |
| Интерфейс | Qt Widgets, трей | Qt Widgets, StatusNotifierItem/AppIndicator |
| Горячие клавиши | `RegisterHotKey` | X11 `XGrabKey`; Wayland — портал `GlobalShortcuts` или CLI + сочетания рабочего стола |
| Захват выделения | `SendInput` Ctrl+C + буфер | X11 XTest; Wayland — копирование пользователем |
| Вставка перевода | `SendInput` Ctrl+V | X11 XTest; Wayland — недоступно надёжно |
| Автозагрузка | реестр `Run` | XDG autostart |
| Секреты | DPAPI | libsecret; fallback-файл 0600 |
| Настройки | `%APPDATA%\qtranslate` | `$XDG_CONFIG_HOME/qtranslate` |
| Пакеты | установщик + portable zip | `tar.gz`, `.deb`, AppImage |

Windows-специфичные ограничения (администраторские окна, Win+V, DPI) на Linux не действуют;
вместо них появляются ограничения Wayland (§4.14).

---

## 2. Общая схема

```
                         ┌────────────────────────────┐
                         │           main.cpp         │
                         │ QApplication, SingleInstance│
                         └──────────────┬─────────────┘
                                        │ создаёт
                                        ▼
┌───────────────────────────────────────────────────────────────────────────┐
│                               AppController                               │
│  связывает модули, реализует сценарии (Ctrl+Q, Ctrl+W, применение настроек)│
└──┬──────────┬──────────────┬───────────────┬─────────────┬────────────┬───┘
   │          │              │               │             │            │
   ▼          ▼              ▼               ▼             ▼            ▼
┌──────┐ ┌──────────┐ ┌─────────────┐ ┌──────────────┐ ┌────────┐ ┌──────────┐
│ UI   │ │ Hotkey   │ │ Selection   │ │ Translation  │ │Settings│ │Autostart │
│      │ │ Manager  │ │ Grabber     │ │ Manager      │ │ Store  │ │          │
│Tray  │ │(WinAPI)  │ │(WinAPI)     │ │ кэш, языки,  │ │ INI +  │ │(реестр)  │
│Main  │ └──────────┘ └─────────────┘ │ отмена       │ │ DPAPI  │ └──────────┘
│Popup │                              └──────┬───────┘ └────────┘
│Setts │                                     │ TranslationService (интерфейс)
└──────┘                           ┌─────────┴──────────┐
                                   ▼                    ▼
                          ┌─────────────────┐  ┌─────────────────┐
                          │ GoogleTranslate │  │ DeepLService    │
                          │ Service         │  │                 │
                          └────────┬────────┘  └────────┬────────┘
                                   └─────────┬──────────┘
                                             ▼
                                  ┌─────────────────────┐
                                  │   NetworkManager    │
                                  │ QNetworkAccessManager│
                                  │ + прокси, таймауты  │
                                  └─────────────────────┘
```

Правила зависимостей (сверху вниз, обратных зависимостей нет):

1. `ui/` знает о `core/` (типы, языки), но не знает о сервисах, сети и WinAPI.
2. `services/` знают о `core/` и `net/`, но не знают о UI.
3. `platform/` не знает ни о чём, кроме Qt.
4. Только `AppController` знает обо всех модулях сразу. Модули общаются через него сигналами/слотами.

### 2.1. Модель потоков

Всё работает в **одном GUI-потоке**. Сеть в Qt асинхронная (`QNetworkReply`), ожидание буфера обмена
делается на `QTimer`, поэтому дополнительные потоки не нужны. Ни одна операция не должна блокировать
цикл событий: никаких `waitForFinished`, `QEventLoop::exec()` внутри обработчиков и `Sleep()`.

---

## 3. Структура исходников

### 3.1. Дерево каталогов

```
qtranslate/
├── CMakeLists.txt
├── .github/
│   └── workflows/build.yml     сборка, тесты, упаковка и релиз
├── packaging/
│   └── windows/
│       ├── qtranslate.iss      установщик Inno Setup
│       └── package.ps1         dist + zip + setup + SHA256SUMS
├── doc/
│   ├── architecture.md
│   ├── plan.md
│   └── releases/0.9.0.md       заметки к релизу
├── resources/
│   ├── qtranslate.qrc          иконки трея, окна
│   ├── icons/                  app.ico, tray-*.png (светлая/тёмная тема)
│   └── qtranslate.rc           иконка и версия exe
├── i18n/
│   └── qtranslate_ru_RU.ts     перевод интерфейса
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── appcontroller.h/.cpp
│   │   ├── logging.h/.cpp            категория логирования qtranslate.app
│   │   └── singleinstance.h/.cpp
│   ├── core/                   ← статическая библиотека qtranslate_core (без Widgets и WinAPI)
│   │   ├── appinfo.h/.cpp            имя и версия приложения
│   │   ├── translationtypes.h        TranslationRequest/Result/Error
│   │   ├── translationservice.h/.cpp интерфейс сервиса + TranslationReply
│   │   ├── translationmanager.h/.cpp реестр сервисов, кэш, логика языков, отмена
│   │   ├── languages.h/.cpp          справочник языков и маппинг кодов
│   │   ├── popupplacement.h/.cpp     позиционирование popup у курсора
│   │   ├── textchunker.h/.cpp        разбиение длинного текста на части
│   │   ├── settings.h/.cpp           AppSettings + SettingsStore
│   │   └── hotkeysequence.h/.cpp     разбор/сериализация сочетаний
│   ├── net/
│   │   └── networkmanager.h/.cpp     QNAM, прокси, таймауты, User-Agent
│   ├── services/
│   │   ├── googletranslateservice.h/.cpp
│   │   ├── googleparser.h/.cpp       чистые функции разбора ответа
│   │   ├── deeplservice.h/.cpp
│   │   └── deeplparser.h/.cpp
│   ├── platform/
│   │   ├── hotkeymanager.h           платформонезависимый интерфейс
│   │   ├── hotkeymanager_win.cpp
│   │   ├── hotkeymanager_stub.cpp    заглушка для не-Windows сборок
│   │   ├── selectiongrabber.h
│   │   ├── selectiongrabber_win.cpp
│   │   ├── selectiongrabber_stub.cpp заглушка для не-Windows сборок
│   │   ├── autostart.h
│   │   ├── autostart_win.cpp
│   │   ├── secretprotector.h
│   │   ├── secretprotector_win.cpp   DPAPI
│   │   ├── windowactivation.h        право вывести окно на передний план
│   │   ├── windowactivation_win.cpp  AllowSetForegroundWindow
│   │   └── windowactivation_stub.cpp заглушка для не-Windows сборок
│   └── ui/
│       ├── trayicon.h/.cpp
│       ├── mainwindow.h/.cpp
│       ├── popupwindow.h/.cpp
│       ├── settingsdialog.h/.cpp
│       ├── languagecombobox.h/.cpp
│       ├── translationerrortext.h/.cpp  текст ошибки перевода для пользователя
│       └── appicon.h/.cpp
└── tests/
    ├── CMakeLists.txt
    ├── fixtures/                     сохранённые ответы Google/DeepL (google/, deepl/)
    ├── tst_googleparser.cpp
    ├── tst_googletranslateservice.cpp  сервис против локального HTTP-сервера
    ├── tst_deeplparser.cpp
    ├── tst_deeplservice.cpp
    ├── tst_languages.cpp
    ├── tst_networkmanager.cpp
    ├── tst_textchunker.cpp
    ├── tst_translationmanager.cpp
    ├── tst_hotkeysequence.cpp
    ├── tst_popupplacement.cpp
    ├── tst_popupwindow.cpp
    ├── tst_secretprotector.cpp
    ├── tst_settings.cpp
    ├── tst_settingsdialog.cpp
    └── tst_autostart.cpp
```

### 3.2. Соответствие текущему `CMakeLists.txt`

Структура §3.1 — целевая: каталоги и файлы появляются и добавляются в цели по мере реализации
этапов. Сейчас:

| Цель | Что входит сейчас (0.9.0) |
|---|---|
| `qtranslate_core` | `core/`: `appinfo`, `hotkeysequence`, `languages`, `popupplacement`, `settings`, `textchunker`, `translationtypes`, `translationservice`, `translationmanager` (кэш, умный язык); `net/networkmanager`; `services/`: `googleparser`, `googletranslateservice`, `deeplparser`, `deeplservice` |
| `qtranslate` | `main.cpp`, `app/*` (включая файловый лог), `ui/*` (`appicon`, `languagecombobox`, `mainwindow` с автопереводом, `popupwindow`, `settingsdialog`, `translationerrortext`, `trayicon`), `platform/*`, `resources/qtranslate.rc`, `resources/icons/app.ico`, `i18n/qtranslate_ru_RU.ts` |
| тесты (`tests/CMakeLists.txt`) | `tst_appinfo`, `tst_autostart` (Windows), `tst_deeplparser`, `tst_deeplservice`, `tst_googleparser`, `tst_googletranslateservice`, `tst_hotkeymanager` (Windows), `tst_hotkeysequence`, `tst_languages`, `tst_networkmanager`, `tst_popupplacement`, `tst_popupwindow` (offscreen), `tst_secretprotector` (Windows), `tst_settings`, `tst_settingsdialog` (offscreen), `tst_textchunker`, `tst_translationmanager` (кэш и smart retry); каждый тест подключается функцией `qtranslate_add_test(<имя>)`, линкуется с `qtranslate_core` и получает путь к `tests/fixtures` в макросе `QTRANSLATE_FIXTURES_DIR` |

### 3.3. Сборка

- `qtranslate_core` — `STATIC`-библиотека из `core/`, `net/`, `services/`; зависит от `Qt::Core`,
  `Qt::Gui` (из-за `QKeySequence` в `hotkeysequence`) и `Qt::Network`, но не от `Qt::Widgets` и не
  использует WinAPI. С ней линкуются и приложение, и тесты.
- `qtranslate` — исполняемый файл (`WIN32`): `app/`, `platform/`, `ui/`, ресурсы; линкуется с
  `qtranslate_core`, `Qt::Widgets`, `user32`, `crypt32`.
- Платформенные файлы подключаются условием `if(WIN32)`; для остальных платформ у каждого модуля
  есть заглушка, чтобы цели собирались. Заголовки в `platform/` не содержат `<windows.h>` —
  WinAPI виден только в `*_win.cpp`.
- Перевод интерфейса: `qt_add_translations(qtranslate TS_FILES i18n/qtranslate_ru_RU.ts)`,
  `.qm` встраиваются в ресурсы.
- `enable_testing()` + `add_subdirectory(tests)`, каждый тест — отдельный `qt_add_executable` и `add_test`.
- Развёртывание — `qt_generate_deploy_app_script` (windeployqt). В дистрибутив попадают плагин
  `tls/qschannelbackend.dll` и MSVC CRT (`vcruntime140*.dll`, `msvcp140*.dll`, `concrt140.dll`):
  `windeployqt` не находит runtime без `VCINSTALLDIR`, поэтому CMake копирует его сам. Это нужно
  для запуска на Windows без установленного Visual C++ Redistributable.

### 3.4. Упаковка, CI и выпуск

Версия проекта задаётся один раз — `project(qtranslate VERSION 0.9.0)`; из неё получаются
`AppInfo::version()`, `User-Agent`, ресурс версии exe и имена артефактов. Тег релиза имеет вид
`v0.9.0`, CI сверяет его с `CMakeLists.txt` и падает при расхождении.

**Портативный архив.** `cmake --install <build> --prefix build/dist` создаёт самодостаточный
каталог `bin/`, `plugins/`, `translations/` с Qt, плагинами и MSVC CRT. `package.ps1` проверяет
обязательные файлы и упаковывает содержимое `dist` в `qtranslate-<версия>-win64.zip`; внутри
архива лежат те же `bin/`, `plugins/`, `translations/`, запуск — `bin\qtranslate.exe`.

**Установщик.** `packaging/windows/qtranslate.iss` (Inno Setup 6.3+ / 7) ставит приложение
**для текущего пользователя**: `PrivilegesRequired=lowest`, каталог
`%LOCALAPPDATA%\Programs\qtranslate`, меню «Пуск», необязательный ярлык на рабочем столе,
деинсталлятор. UAC не запрашивается. Языки установщика — English и Русский. Настройки в
`%APPDATA%\qtranslate` не удаляются при деинсталляции. Установщик и деинсталлятор проверяют
`AppMutex=Local\qtranslate-app`, который держит запущенное приложение (`main.cpp`), поэтому файлы
не заменяются и не удаляются под работающей копией. `CloseApplications=yes` дополнительно
позволяет Restart Manager закрыть приложение, если пользователь согласится.

**Скрипт.** `packaging/windows/package.ps1` делает `cmake --install`, проверяет наличие
`qtranslate.exe`, Qt DLL, `qwindows.dll`, `qschannelbackend.dll` и CRT, собирает zip и установщик,
затем пишет `SHA256SUMS.txt`. Параметры `-SkipInstall` и `-SkipInstaller` нужны для отладки
упаковки; CI использует полный путь.

**CI.** `.github/workflows/build.yml` на `windows-latest`:
1. `install-qt-action` ставит Qt 6.8.3 LTS MSVC 2022 x64 (aqtinstall пока не поддерживает
   раскладку репозитория Qt 6.12; локальная разработка ведётся на 6.12, проект требует 6.5+);
2. CMake (Visual Studio 2022 generator) собирает Release;
3. `ctest` запускает 17 тестов с `QT_QPA_PLATFORM=offscreen`;
4. Chocolatey ставит Inno Setup, `package.ps1` собирает артефакты;
5. `upload-artifact` сохраняет `build/artifacts/*`.

Workflow запускается на push в `main`, pull request и вручную. По тегу `v*` отдельная job с
`contents: write` скачивает артефакты и создаёт/обновляет GitHub Release, прикладывая установщик,
zip и `SHA256SUMS.txt`; заметки берутся из `doc/releases/<версия>.md`. Повторный запуск
перезаписывает файлы релиза (`gh release upload --clobber`).

**Подпись.** Кодовой подписи пока нет: сертификата в проекте нет, SmartScreen может показать
предупреждение. Добавление подписи не меняет формат артефактов и CI-шагов (нужен секрет с
сертификатом и вызов `signtool`).

---

## 4. Модули

### 4.1. `main.cpp` и `SingleInstance`

`main()`:

1. Создаёт `QApplication`, задаёт `applicationName`/`applicationVersion`,
   `setQuitOnLastWindowClosed(false)` — иначе скрытие последнего окна завершит процесс.
   `organizationName` намеренно не задаётся, чтобы данные лежали в `%APPDATA%\qtranslate`
   (см. §4.11). На Windows дополнительно создаётся именованный mutex `Local\qtranslate-app`,
   который проверяют установщик и деинсталлятор (`AppMutex`, §3.4).
2. Разбирает аргументы (`QCommandLineParser`): `--minimized` (передаётся из автозагрузки) и
   `--verbose` (подробный лог).
3. `SingleInstance` (ключ `qtranslate-<имя пользователя>`):
   - Основной экземпляр выбирается захватом файла блокировки `%TEMP%\qtranslate-<user>.lock`
     (`QLockFile`). Это атомарно, а блокировка упавшего процесса определяется по PID и
     перехватывается. Одного `QLocalServer` для этого недостаточно: между неудачной попыткой
     подключиться и `listen()` есть окно гонки, если два экземпляра стартуют одновременно.
   - Не удалось захватить файл — программа уже запущена: подключаемся к её `QLocalServer`
     (повторяя попытки до 1 с, если сервер ещё не поднят), отправляем `activate`
     (или `noop` при `--minimized`) и завершаемся с кодом 0. Перед `activate` вызывается
     `AllowSetForegroundWindow(ASFW_ANY)`, чтобы основной экземпляр мог вывести окно на передний план.
   - Захватили — поднимаем `QLocalServer` и по команде `activate` показываем главное окно.
4. Загружает настройки, устанавливает `QTranslator` для языка интерфейса.
5. Создаёт `AppController` и запускает цикл событий.

Без защиты от второго экземпляра повторный запуск из ярлыка (или автозагрузка плюс ручной старт)
дал бы два процесса, которые не поделили бы горячие клавиши.

### 4.2. `AppController`

Единственный класс, который знает обо всех модулях. Владеет ими (через дерево `QObject` или
`std::unique_ptr`) и реализует сценарии из §5.

Ответственность:

- создание модулей в правильном порядке (§5.1);
- обработка `HotkeyManager::activated(action)` → захват выделения → перевод → показ результата;
- применение изменённых настроек (`SettingsStore::changed`): перерегистрация клавиш, смена прокси,
  синхронизация автозагрузки, смена сервиса;
- показ уведомлений через трей (клавиша занята, ошибка автозагрузки и т.п.);
- корректное завершение: снять регистрацию клавиш, сохранить геометрию окон.

`AppController` не содержит бизнес-логики перевода (она в `TranslationManager`) и не рисует UI.

### 4.3. Горячие клавиши — `HotkeyManager`

```cpp
enum class HotkeyAction { TranslateToPopup, TranslateAndReplace, TranslateToMainWindow,
                           ShowMainWindow };

class HotkeyManager : public QObject {
    Q_OBJECT
public:
    // false — сочетание занято другой программой или не поддерживается
    bool registerHotkey(HotkeyAction action, const QKeySequence &seq);
    void unregisterHotkey(HotkeyAction action);
    void unregisterAll();
    // Временно отключить всё (пока пользователь вводит новое сочетание в настройках)
    void setSuspended(bool suspended);
    bool isSuspended() const;
    QKeySequence hotkey(HotkeyAction action) const;
signals:
    void activated(HotkeyAction action);
    // Причина — текст WinAPI (например, «Hot key is already registered»)
    void registrationFailed(HotkeyAction action, const QKeySequence &seq,
                            const QString &reason);
};
```

Реализация под Windows (`hotkeymanager_win.cpp`):

- `RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk)`. С `hWnd = nullptr` сообщение
  `WM_HOTKEY` приходит в очередь потока; его ловит `QAbstractNativeEventFilter`, установленный через
  `QCoreApplication::installNativeEventFilter` (тип события `"windows_generic_MSG"`).
- `MOD_NOREPEAT` исключает автоповтор, если клавишу держат нажатой.
- Перевод `QKeySequence` → `(MOD_*, VK_*)` — в `core/hotkeysequence` (чистая функция, покрыта
  тестами): буквы и цифры совпадают с ASCII, `F1–F24`, `Insert`, `Pause` и т.п. — по таблице.
- Если `RegisterHotKey` вернул ошибку (`ERROR_HOTKEY_ALREADY_REGISTERED`), метод возвращает `false`,
  испускает `registrationFailed` с текстом ошибки, а `AppController` показывает уведомление в трее с
  предложением сменить сочетание.
- Идентификатор регистрации — `1 + int(action)` (диапазон `RegisterHotKey` 0x0001–0xBFFF).
  `setSuspended(true)` снимает регистрацию, но запоминает сочетания; `setSuspended(false)`
  регистрирует их заново и сообщает об ошибках через тот же сигнал.
- При старте регистрируются три сочетания по умолчанию: Ctrl+Q (`TranslateToPopup`),
  Ctrl+Shift+Q (`TranslateAndReplace`) и Ctrl+W (`TranslateToMainWindow`). `ShowMainWindow` пока не
  назначено.

Настройки по умолчанию:

| Действие | По умолчанию |
|---|---|
| `TranslateToPopup` | `Ctrl+Q` |
| `TranslateAndReplace` | `Ctrl+Shift+Q` |
| `TranslateToMainWindow` | `Ctrl+W` |
| `ShowMainWindow` | не назначено |

> **Решение.** Глобальная регистрация перехватывает сочетание у **всех** программ: Ctrl+W перестанет
> закрывать вкладки в браузере и редакторах, Ctrl+Q — закрывать программы. Владелец проекта эти
> сочетания для закрытия не использует, поэтому Ctrl+Q / Ctrl+Shift+Q / Ctrl+W остаются значениями
> по умолчанию, без предупреждений и мастера первого запуска. Сменить их можно в настройках.
> Срабатывание «только при наличии выделения» невозможно: в момент нажатия неизвестно, есть ли
> выделение.

### 4.4. Захват выделенного текста — `SelectionGrabber`

В Windows нет общесистемного «выделения», поэтому текст получаем через буфер обмена:
имитируем Ctrl+C в активном окне и читаем результат.

```cpp
class SelectionGrabber : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &text)>; // пустая строка — ничего не выделено
    void grab(Callback cb);       // асинхронно; повторный вызов во время захвата игнорируется
    bool isBusy() const;
    void cancel();                // остановить захват и восстановить буфер
    void setMaxLength(int length);
    void setRestoreClipboard(bool restore);
};
```

Алгоритм (конечный автомат на `QTimer`, без блокировок):

1. **Ожидание отпускания клавиш.** Пользователь ещё держит Ctrl и W. Опрашиваем `GetAsyncKeyState`
   каждые 10 мс, пока не отпущены модификаторы, но не дольше 300 мс. Если Alt/Shift/Win всё ещё
   нажаты, отправляем для них синтетическое отпускание, иначе приложение получит Ctrl+Alt+C и т.п.
2. **Снимок буфера.** Копируем все форматы текущего `QMimeData` (только если включено
   «восстанавливать буфер обмена», по умолчанию включено). Запоминаем
   `GetClipboardSequenceNumber()`.
3. **Ctrl+C.** `SendInput` с последовательностью Ctrl↓ C↓ C↑ Ctrl↑.
4. **Ожидание изменения.** Каждые 10 мс сравниваем `GetClipboardSequenceNumber()` с запомненным,
   максимум 400 мс. Не изменился — выделения нет, `cb("")`.
5. **Чтение.** `CF_UNICODETEXT` читается напрямую через WinAPI, а не через `QClipboard`: после
   смены sequence number кэш Qt может ещё хранить старые данные. Текст обрезается по краям и
   ограничивается длиной (`translation/maxLength`, по умолчанию 20 000 символов; разрез не рвёт
   суррогатную пару).
6. **Восстановление.** Возвращаем сохранённый снимок. К восстановленным данным добавляем форматы
   `ExcludeClipboardContentFromMonitorProcessing` и `CanIncludeInClipboardHistory = 0`, чтобы
   восстановление не засоряло журнал буфера (Win+V) и менеджеры буфера обмена.
7. `cb(text)` ровно один раз.

Ограничения (описать в справке):

- **UIPI.** Обычный процесс не может отправить ввод в окно процесса, запущенного от администратора.
  Выделение в таких окнах не захватывается; `SendInput` при этом молча ничего не делает.
- Программы, где Ctrl+C означает другое (терминалы — прерывание), или где копирование отключено.
- Промежуточный Ctrl+C выполняет сама source-программа, без exclusion-форматов, поэтому запись в
  журнале Win+V может появиться. Форматы на шаге 6 защищают только восстановление буфера. Полностью
  избежать этого позволяет стратегия через UI Automation (§9.2); до неё ограничение считается
  принятым.

Возможное развитие: стратегия через UI Automation (`IUIAutomationTextPattern::GetSelection`),
которая не трогает буфер обмена. Интерфейс `grab()` не изменится: внутри появится цепочка стратегий
«UIA → буфер обмена».

### 4.5. Перевод: типы и интерфейс сервиса

`core/translationtypes.h`:

```cpp
struct TranslationRequest {
    QString text;
    QString sourceLang;   // внутренний код или "auto"
    QString targetLang;   // внутренний код
};

enum class TranslationError {
    None,
    Network,        // нет соединения, ошибка прокси, DNS, TLS
    Timeout,
    Auth,           // неверный/отсутствующий API-ключ (HTTP 401/403)
    QuotaExceeded,  // DeepL 456, лимит Google Cloud
    RateLimited,    // HTTP 429
    TooLong,        // превышен лимит размера запроса
    UnsupportedLanguage,
    BadResponse,    // ответ не удалось разобрать
    Cancelled,
    EmptyText,      // нечего переводить; определяется до запроса
};

struct TranslationResult {
    QString translatedText;
    QString detectedSourceLang;   // внутренний код, если сервис его вернул
    QString serviceId;
    TranslationError error = TranslationError::None;
    QString errorMessage;         // технические подробности для пользователя/лога, без самого текста
    bool ok() const;
};
```

Текст ошибки для пользователя собирает UI (`ui/translationerrortext`): заголовок по виду ошибки
(переводимый) и в скобках `errorMessage`, например «Network error (Host … not found)».

`core/translationservice.h`:

```cpp
class TranslationReply : public QObject {
    Q_OBJECT
public:
    // Готовый ответ для ошибок до сети: finished() придёт через цикл событий
    static TranslationReply *createFinished(const TranslationResult &r, QObject *parent = nullptr);
    const TranslationResult &result() const;
    bool isFinished() const;
    void abort();                 // → error = Cancelled, finished() испускается один раз
signals:
    void finished();              // ровно один раз и никогда — изнутри translate()
protected:
    void finish(const TranslationResult &r);   // для реализаций; повторные вызовы игнорируются
    virtual void abortImpl();                  // остановить сетевую работу
};

class TranslationService : public QObject {
    Q_OBJECT
public:
    virtual QString id() const = 0;            // "google", "deepl"
    virtual QString displayName() const = 0;
    virtual bool isConfigured() const = 0;     // DeepL без ключа — false
    virtual QStringList sourceLanguages() const = 0;  // внутренние коды, без "auto"
    virtual QStringList targetLanguages() const = 0;
    virtual void applySettings(const AppSettings &s) = 0;   // ключи, режим, вариант
    // Возвращает объект ответа; владеет им вызывающий (deleteLater после finished)
    virtual TranslationReply *translate(const TranslationRequest &req) = 0;
};
```

- `abort()` сначала завершает ответ с `Cancelled`, а потом вызывает `abortImpl()`. Поэтому ошибка,
  которую сообщит прерванный сетевой запрос, уже ничего не меняет.
- `finished()` не испускается внутри `translate()`, даже если ответ известен сразу (неподдерживаемый
  язык, пустой текст): вызывающий должен успеть подключиться. Для этого есть `createFinished()`.
- Вызывающий может удалить ответ и до `finished()` — это отменяет запрос без сигнала.
- `applySettings()` применяет ключи, режим Google и вариант английского DeepL без пересоздания
  сервисов.

Каждый сервис получает указатель на `NetworkManager` (общий `QNetworkAccessManager` и
`makeRequest()` с таймаутом и `User-Agent`) и не создаёт свой менеджер — так прокси
настраивается в одном месте.

Разбор ответов вынесен в чистые функции (`googleparser`, `deeplparser`): `QByteArray → TranslationResult`.
Это позволяет тестировать их на сохранённых ответах без сети.

### 4.6. `GoogleTranslateService`

Два режима, выбираются в настройках:

**1. Бесплатный (без ключа, по умолчанию).** Неофициальный эндпоинт, которым пользуются
расширения браузеров:

```
POST https://translate.googleapis.com/translate_a/single?client=gtx&sl=<src|auto>&tl=<dst>&dt=t
Content-Type: application/x-www-form-urlencoded

q=<текст>
```

POST вместо GET — чтобы не упираться в длину URL. Ответ — вложенные JSON-массивы:
`data[0]` — список предложений `[перевод, оригинал, …]` (переводы склеиваются),
`data[2]` — код определённого исходного языка.

Риски: эндпоинт не документирован, может измениться или вернуть HTTP 429 при частых запросах.
Парсер должен быть терпимым к лишним/отсутствующим полям, а при неожиданном формате возвращать
`BadResponse`, а не падать.

Что известно по ответам эндпоинта (этап 2) и как это учтено в `GoogleParser::parseFreeResponse`:

- строки `data[0]`, где первый элемент — не строка (транслитерация `[null, null, "…"]` при
  `dt=rm`), пропускаются;
- язык берётся из `data[2]`, если его нет — из `data[8][0][0]`; код переводится во внутренний с
  учётом синонимов: иврит приходит как `iw`, китайский — как `zh-CN` (§4.8). Неизвестный код —
  пустой `detectedSourceLang`, а не ошибка;
- старые версии отдавали «разреженные» массивы (`[,,"en"]`, это не JSON) — перед повторным разбором
  в пропуски вставляется `null`;
- неизвестный код целевого языка ошибки не даёт: приходит HTTP 200 с непереведённым текстом. Поэтому
  коды проверяются до запроса: язык не из таблицы → сразу `UnsupportedLanguage`.

Ошибки бесплатного режима (`GoogleParser::errorForFreeHttpStatus` и сетевой уровень):

| Ситуация | `TranslationError` |
|---|---|
| HTTP 429, 403 (эндпоинт так блокирует частые запросы) | `RateLimited` |
| HTTP 413, 414 | `TooLong` |
| HTTP 5xx | `Network` (`errorMessage` = «HTTP 503») |
| прочие не-2xx | `BadResponse` |
| таймаут передачи (`TimeoutError`/`OperationCanceledError` без нашего `abort()`) | `Timeout` |
| прочие ошибки `QNetworkReply` | `Network`, `errorMessage` = текст ошибки Qt |

Текст ошибки Qt содержит URL запроса. В бесплатном режиме в URL нет ни текста, ни секретов; в режиме
Cloud API (ключ в `?key=`) этот текст нельзя показывать и писать в лог как есть — учесть на этапе 6.

Для тестов адрес эндпоинта подменяется (`setEndpoint()`): `tst_googletranslateservice` поднимает
локальный HTTP-сервер.

**2. Google Cloud Translation API v2 (с ключом).**

```
POST https://translation.googleapis.com/language/translate/v2?key=<KEY>
Content-Type: application/json

{"q": "…", "target": "ru", "source": "en", "format": "text"}
```

Ответ: `data.translations[].translatedText` и `detectedSourceLanguage`. `source` не передаётся,
если выбран «auto».

Длинный текст режется `TextChunker` на части до 5000 символов (UTF-16) по границам
абзацев/предложений. Пробный POST на ~6900 символов к gtx проходит, но лимит не документирован,
поэтому разбиение делается всегда. Части отправляются последовательно, результат склеивается; при
ошибке любой части запрос завершается с этой ошибкой. Пробелы и переводы строк на краях каждой
части в запрос не попадают и возвращаются на место при склейке — сервис может их не сохранить, а на
стыках частей это сломало бы форматирование. Определённый язык берётся из первой части, где он есть.

Ошибки Cloud API: 401/403 → `Auth`, 429 → `RateLimited`, 413 → `TooLong`, 5xx → `Network`, прочее
→ `BadResponse`. Google отвечает на неверный ключ кодом 400 с текстом «API key not valid», поэтому
`errorForCloudResponse(status, body)` проверяет тело и относит такой 400 к `Auth`; `quotaExceeded` и
`rateLimitExceeded` в теле 400/403 дают соответствующие ошибки. В `errorMessage` попадает только
`HTTP <код>`, без URL и ключа. Для тестов адрес подменяется `setCloudEndpoint()`.

### 4.7. `DeepLService`

Требует API-ключ (бесплатный план DeepL API Free или платный Pro). Без ключа
`isConfigured() == false`: в UI сервис помечен «требуется ключ», а при попытке перевода
возвращается ошибка `Auth` с подсказкой открыть настройки.

```
POST https://api-free.deepl.com/v2/translate     ← ключ оканчивается на ":fx"
POST https://api.deepl.com/v2/translate          ← иначе
Authorization: DeepL-Auth-Key <KEY>
Content-Type: application/json

{"text": ["…"], "target_lang": "RU", "source_lang": "EN"}
```

- Хост выбирается автоматически по суффиксу ключа `:fx`.
- Авторизация только заголовком; передача ключа в теле/URL у DeepL устарела.
- `source_lang` не передаётся при «auto»; target `en` заменяется на вариант из настроек
  (`EN-US`/`EN-GB`).
- Ответ: `translations[].text`, `translations[].detected_source_language`.
- Коды ошибок: 403 → `Auth`, 456 → `QuotaExceeded`, 429 → `RateLimited`, 413 → `TooLong`, 5xx →
  `Network`; в `errorMessage` — только `HTTP <код>`.
- Лимит тела запроса — 128 КиБ; длинный текст режется `TextChunker` и отправляется **одним**
  запросом массивом `text`. Пробелы на краях частей не отправляются и восстанавливаются при
  склейке ответа по порядку.
- В настройках кнопка «Проверить ключ» вызывает `GET /v2/usage` с ключом из поля ввода и показывает
  `character_count`/`character_limit` или понятную ошибку. Ошибки проверки не содержат ключ.

### 4.8. Языки — `languages`

Внутри программы используются собственные коды языков (`en`, `ru`, `de`, `pt-BR`, `zh-Hans`…).
Сервисы переводят их в свои коды:

| Внутренний | Google | DeepL source | DeepL target |
|---|---|---|---|
| `en` | `en` | `EN` | `EN-US` (настраивается: `EN-GB`) |
| `pt-BR` | `pt` | `PT` | `PT-BR` |
| `zh-Hans` | `zh-CN` | `ZH` | `ZH-HANS` |
| `ru` | `ru` | `RU` | `RU` |
| `he` | `iw` | `HE` | `HE` |
| `nb` | `no` | `NB` | `NB` |
| `fil` | `tl` | — | — |

Модуль хранит таблицу `{код, английское название, название на родном языке, коды сервисов}`
(около 110 языков Google, из них около 35 есть у DeepL) и предоставляет функции в пространстве имён
`Languages`:

- `toServiceCode(serviceId, code, direction)` — пустая строка, если сервис язык не поддерживает;
- `fromServiceCode(serviceId, serviceCode, direction)` — без учёта регистра; сначала код нужного
  направления, затем другого (DeepL `EN-US` как исходный, `PT` как целевой), затем синонимы
  (Google `he`/`jv`/`zh`, DeepL `EN-GB`); неизвестный код — пустая строка. Обратное
  преобразование нужно для `detectedSourceLang`;
- `supported(serviceId, direction)` — списки языков для сервисов и `LanguageComboBox`;
- `closest(serviceId, code, direction)` — ближайший поддерживаемый код: точный, затем язык той же
  основы (`pt-BR`/`pt-PT`, `zh-Hans`/`zh-Hant`), системный, `en`, первый доступный;
- `displayName(code)` — название для UI, помечено `QT_TRANSLATE_NOOP` (контекст `Languages`) под
  локализацию этапа 8; `nativeName(code)` — самоназвание (подсказка в списке);
- `autoCode()` — `"auto"`; `forLocale(QLocale)` / `systemLanguage()` — язык системы с учётом
  письменности и региона (`zh-Hans`/`zh-Hant`, `pt-BR`/`pt-PT`, нюнорск → `nb`), иначе `en`.

Списки языков у сервисов разные (у DeepL их меньше). `LanguageComboBox` показывает только
поддерживаемые выбранным сервисом; при смене сервиса `AppController` заменяет текущий язык через
`Languages::closest` (например, `pt` → `pt-BR`, `fil` → системный/`en`). «Auto-detect» остаётся
исходным языком всегда.

### 4.9. `TranslationManager`

Фасад над сервисами, с которым работают `AppController` и UI.

```cpp
class TranslationManager : public QObject {
    Q_OBJECT
public:
    enum class Channel { MainWindow, Popup, InPlace };

    void registerService(TranslationService *service);   // забирает владение; первый — активный
    QList<TranslationService *> services() const;
    TranslationService *service(const QString &id) const;
    TranslationService *activeService() const;
    void setActiveService(const QString &id);
    void setMaxLength(int length);                         // 20 000 по умолчанию; из настроек — этап 5

    // channel — «канал» запроса: Popup, MainWindow или InPlace.
    // Новый запрос в том же канале отменяет предыдущий незавершённый.
    TranslationReply *translate(const TranslationRequest &req, Channel channel);
    void cancel(Channel channel);
    void cancelAll();                                      // при завершении программы (§5.6)
signals:
    void activeServiceChanged(const QString &id);
};
```

Менеджер хранит на текущий ответ каждого канала только `QPointer`: ответом владеет вызывающий.
`translate()` сначала проверяет кэш, затем оборачивает ответ сервиса в `Reply` (кэширование и
повтор на дополнительный язык); интерфейс менеджера от этого не меняется. Отменённый ответ
приходит вызывающему с `Cancelled`, и UI его не показывает.

Ответственность:

1. **Отмена устаревших запросов.** Пользователь нажал Ctrl+Q, не дождался и выделил другой текст —
   первый запрос отменяется, его результат не покажется поверх нового. Каналы независимы:
   перевод в главном окне не отменяет всплывающий.
2. **Умный выбор языка перевода.** В настройках два языка: основной (например, `ru`) и
   дополнительный (`en`). Если исходный язык «auto» и сервис определил, что текст уже на основном
   языке, запрос автоматически повторяется с переводом на дополнительный. Так одна клавиша
   работает в обе стороны: английский → русский и русский → английский.
3. **Кэш.** LRU на 200 записей, ключ — `serviceId|src|dst|text`. Повторный перевод того же
   фрагмента мгновенный и не тратит квоту DeepL. Кэш только в памяти.
4. **Проверки до сети.** Пустой текст (`EmptyText`), превышение `maxLength` (`TooLong`),
   ненастроенный сервис (`Auth`) — ошибка возвращается сразу, без запроса (через
   `TranslationReply::createFinished`, то есть асинхронно).

### 4.10. Сеть и прокси — `NetworkManager`

Владеет единственным `QNetworkAccessManager` приложения.

```cpp
enum class ProxyMode { None, System, Manual };
enum class ProxyType { Http, Socks5 };

struct ProxySettings {
    ProxyMode mode = ProxyMode::System;
    ProxyType type = ProxyType::Http;
    QString host;
    quint16 port = 0;
    bool authEnabled = false;
    QString user;
    QString password;
};

class NetworkManager : public QObject {
    Q_OBJECT
public:
    QNetworkAccessManager *nam();
    void applyProxy(const ProxySettings &p);
    const ProxySettings &proxySettings() const;
    QNetworkRequest makeRequest(const QUrl &url) const;   // таймаут, User-Agent, HTTP/1.1
    // Проверка прокси без сохранения: временный QNAM с заданными параметрами
    void testProxy(const ProxySettings &p, const QUrl &url,
                   std::function<void(bool ok, const QString &message)> cb);
    QString describeNetworkError(QNetworkReply *reply, bool includeDetails = true) const;
};
```

Сопоставление с Qt:

| Режим / тип | Реализация |
|---|---|
| Без прокси | `QNetworkProxy::NoProxy` |
| Системный | `QNetworkProxyFactory::setUseSystemConfiguration(true)` (WinINet/WPAD, PAC) |
| HTTP | `QNetworkProxy::HttpProxy`; для https-адресов Qt сам делает туннель `CONNECT` |
| SOCKS5 | `QNetworkProxy::Socks5Proxy`; DNS-имена резолвятся на стороне прокси |

Логин и пароль задаются прямо в `QNetworkProxy`. `proxyAuthenticationRequired` не показывает
диалог: при наличии данных они передаются один раз; повторный запрос завершается отказом. Ошибки
прокси (`ProxyAuthenticationRequiredError`, `ProxyConnectionRefusedError`, `ProxyNotFoundError`,
`ProxyTimeoutError` и т.п.) превращаются в короткие сообщения «Proxy authentication failed» /
«Proxy connection failed» через `describeNetworkError`; остальные ошибки сохраняют текст Qt. Для
Cloud-URL с ключом `includeDetails = false` оставляет только общий «Network error».

После `applyProxy` вызывается `nam->clearConnectionCache()`, чтобы уже открытые keep-alive
соединения не продолжали идти мимо нового прокси. `testProxy` использует такой же временный QNAM,
те же таймаут/User-Agent и считает успехом любой HTTP-ответ; в UI проверка идёт на
`https://translate.googleapis.com/`.

**О «HTTPS-прокси» (решение принято).** Под HTTPS-прокси в проекте понимается HTTP-прокси,
через который идут https-запросы (туннель `CONNECT`). Отдельного типа для этого нет: тип `Http`
обслуживает и http-, и https-адреса. Прокси с TLS-соединением *до самого прокси* `QNetworkProxy`
не поддерживает, и в проекте он не нужен.

SOCKS4 Qt не поддерживает; в UI предлагается только SOCKS5.

Общие параметры запросов: `setTransferTimeout(10 000 мс)` (настраивается), постоянный
`User-Agent: qtranslate/<версия>`, только HTTPS.

**HTTP/1.1 для бесплатного Google.** Qt 6.12 по умолчанию согласует HTTP/2, и
`translate.googleapis.com/translate_a/single` в наших проверках стабильно отвечал на такие запросы
`HTTP 429`, хотя те же GET/POST по HTTP/1.1 (curl, .NET) проходили. Поэтому `makeRequest()` ставит
`QNetworkRequest::Http2AllowedAttribute = false`; после этого gtx переводит без 429. Запросы ко всем
сервисам идут по HTTP/1.1 — для DeepL это не имеет значения, а поведение бесплатного Google
становится предсказуемым. Регрессия проверяется в `tst_networkmanager::requestsUseHttp11`.

**Состояние на этапе 8.** Реализованы `applyProxy`, `testProxy`, обработка авторизации/ошибок
прокси и `describeNetworkError`. Режимы `None`/`System`/`Manual`, типы HTTP/SOCKS5; после смены
прокси кэш соединений сбрасывается. Прокси применяется при старте и при сохранении настроек.
Системный режим и ручной HTTP-прокси проверены на реальном прокси `127.0.0.1:2080`.

### 4.11. Настройки — `AppSettings` и `SettingsStore`

`AppSettings` — обычная структура-значение со всеми параметрами (вложенные группы `general`,
`hotkeys`, `translation`, `google`, `deepl`, `network`, `popup`, `mainWindow` и
`settingsVersion`). `SettingsStore` загружает и сохраняет её и сообщает об изменениях:

```cpp
class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(QObject *parent = nullptr);
    SettingsStore(const QString &filePath, const SecretCodec &codec,
                  QObject *parent = nullptr);
    static QString defaultFilePath(const QString &applicationDir,
                                   const QString &appDataDir);
    const AppSettings &current() const;
    void setSecretCodec(const SecretCodec &codec);
    void load();
    void save(const AppSettings &s);       // пишет на диск и испускает changed
signals:
    void changed(const AppSettings &oldS, const AppSettings &newS);
};
```

Окно настроек редактирует **копию** `AppSettings`; по «ОК» отдаёт её в `save()`.
`AppController` сравнивает старое и новое значение и применяет только изменившееся
(перерегистрирует клавиши, меняет языки, автозагрузку и т.д.).

Хранение: INI-файл через `QSettings`.

- Обычный режим: `%APPDATA%\qtranslate\qtranslate.ini`.
- Портативный режим: если рядом с exe лежит `qtranslate.ini`, используется он. Путь выбирает
  чистая `SettingsStore::defaultFilePath(applicationDir, appDataDir)`, поэтому правило проверяется
  тестом без записи рядом с тестовым exe.

`organizationName` не задан (§4.1), поэтому `QSettings` по умолчанию для этого не годится.
`SettingsStore` открывает INI по явному пути — `QSettings(path, QSettings::IniFormat)`. Группа
`general` в INI называется `[app]`: `QSettings` зарезервировал имя `[General]` для ключей верхнего
уровня и иначе экранирует группу в `[%General]`.

Секреты (API-ключи, пароль прокси) хранятся зашифрованными через Windows DPAPI
(`CryptProtectData`, привязка к учётной записи пользователя) в base64. Модуль
`platform/secretprotector` предоставляет `protect(QString) → QByteArray` и
`unprotect(QByteArray) → QString`. `SettingsStore` не знает WinAPI: приложение передаёт ему
`SecretCodec` из двух `std::function`; без кодека секреты не пишутся вовсе (открытым текстом —
никогда). Если расшифровать не удалось (файл перенесён на другую машину), поле считается пустым.

Ключи файла:

| Ключ | Тип | По умолчанию |
|---|---|---|
| `app/autostart` | bool | `false` |
| `app/startMinimized` | bool | `true` |
| `app/uiLanguage` | string | `system` (переключение — этап 8) |
| `app/restoreClipboard` | bool | `true` |
| `hotkeys/popup` | QKeySequence (portable text) | `Ctrl+Q` |
| `hotkeys/replaceSelection` | QKeySequence | `Ctrl+Shift+Q` |
| `hotkeys/mainWindow` | QKeySequence | `Ctrl+W` |
| `hotkeys/showMainWindow` | QKeySequence | пусто |
| `translation/service` | string | `google` |
| `translation/sourceLang` | string | `auto` |
| `translation/primaryLang` | string | язык системы |
| `translation/secondaryLang` | string | `en` (или `ru`, если системный — английский) |
| `translation/maxLength` | int | `20000` |
| `translation/autoTranslateDelayMs` | int | `700` (0 — выкл.) |
| `google/mode` | `free` \| `cloud` | `free` |
| `google/apiKey` | секрет | пусто |
| `deepl/apiKey` | секрет | пусто |
| `deepl/englishVariant` | `EN-US` \| `EN-GB` | `EN-US` |
| `network/proxyMode` | `none` \| `system` \| `manual` | `system` |
| `network/proxyType` | `http` \| `socks5` | `http` |
| `network/proxyHost`, `proxyPort` | string, int | — |
| `network/proxyAuth`, `proxyUser` | bool, string | — |
| `network/proxyPassword` | секрет | — |
| `network/timeoutMs` | int | `10000` |
| `popup/width`, `popup/height`, `popup/autoCloseOnFocusLoss` | — | `460`, `260`, `true` |
| `mainWindow/geometry`, `mainWindow/state` | QByteArray | — |

В файл пишется `settingsVersion`; при изменении формата миграция выполняется в `SettingsStore::load()`.

### 4.12. Автозагрузка — `Autostart`

```cpp
namespace Autostart {
    QString commandForExecutable(const QString &path);
    bool isEnabled();
    bool setEnabled(bool on);   // false — нет доступа к реестру
    bool sync(bool wanted);     // привести реестр к настройке, обновить путь
    bool isApproved();          // учитывает StartupApproved
}
```

- Запись в `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, значение `qtranslate` =
  `"C:\…\qtranslate.exe" --minimized` (через `QSettings` с `NativeFormat`). Права администратора
  не нужны.
- `sync()` вызывается при каждом старте: если автозагрузка включена, а путь к exe изменился
  (программу переместили или обновили в другую папку), запись обновляется.
- Пользователь может отключить автозагрузку в Диспетчере задач — тогда Windows ставит флаг в
  `…\Explorer\StartupApproved\Run`. Мы его не переписываем, а только показываем в настройках
  «отключено в Диспетчере задач».

### 4.13. Пользовательский интерфейс

#### `TrayIcon`

`QSystemTrayIcon` с меню:

```
Показать окно
Сервис ▸  ● Google Translate
          ○ DeepL
───────────
Настройки…
Выход
```

- Левый клик — показать/скрыть главное окно.
- Подсказка значка: «qtranslate — Ctrl+Q / Ctrl+W» (актуальные сочетания).
- `showMessage()` для уведомлений: занятая горячая клавиша, ошибка сети при выключенном окне,
  первый запуск.
- Если `QSystemTrayIcon::isSystemTrayAvailable()` вернул `false`, главное окно всегда показывается
  при старте, а закрытие окна завершает программу.

#### `PopupWindow` (Ctrl+Q)

```
┌──────────────────────────────────────────────┐
│ EN → RU ▾                    Google   📌  ✕   │
├──────────────────────────────────────────────┤
│ Перевод текста, который можно выделять       │
│ и копировать…                                │
├──────────────────────────────────────────────┤
│ ⧉ Копировать   ⇄ В главное окно              │
└──────────────────────────────────────────────┘
```

- Флаги: `Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint` — без кнопки на панели задач.
- Создаётся лениво при первом Ctrl+Q и переиспользуется, чтобы появляться без задержки.
- Позиция: у курсора в момент нажатия клавиши (точка запоминается до захвата выделения), с
  ограничением рамками `QScreen::availableGeometry()` того монитора, где курсор. Чистая геометрия
  вынесена в `core/popupplacement`: окно сначала ставится справа-снизу, у края отражается
  влево/вверх, затем прижимается к экрану.
- Ширина 460 логических пикселей (настройка — этап 5). Высота подстраивается под текст результата
  до предела `min(600, 70% рабочей области)`; дальше прокрутка.
- Состояния: «загрузка» (анимированный спиннер, показ сразу после захвата выделения), «результат»,
  «ошибка» (текст и кнопка «Повторить»), «нет выделения» (короткая подсказка, закрывается через
  1,5 с).
- Закрытие: Esc и ✕ всегда; потеря фокуса — если не закреплено 📌. Закрепление действует в пределах
  одного вызова и сбрасывается при новом Ctrl+Q. Меню языка — временный popup: пока приложение
  остаётся активным и другое наше окно не стало активным, закрытие меню не закрывает окно; клик в
  другой программе или в главном окне закрывает. Настройка `popup/autoCloseOnFocusLoss` появится на
  этапе 5, пока поведение включено.
- Клик по «Auto → Русский ▾» открывает меню целевых языков; выбор сразу переводит сохранённый
  исходный текст заново. После результата в строке показывается определённый язык.
- «Копировать» кладёт перевод в буфер. «В главное окно» испускает
  `openInMainWindowRequested(request, result)`; `AppController` передаёт оба текста в
  `MainWindow::showTranslation()` без нового запроса и показывает главное окно.
- Скрытие испускает `closed()`; `AppController` отменяет незавершённый запрос канала `Popup`, чтобы
  поздний ответ не показался поверх следующего. Новый Ctrl+Q заменяет старый запрос того же канала.
- Активация: Windows разрешает процессу, получившему `WM_HOTKEY`, вывести окно на передний план,
  поэтому `activateWindow()` после горячей клавиши срабатывает.

#### `MainWindow` (Ctrl+W)

```
┌───────────────────────────────────────────────────────────┐
│ [Авто ▾]  ⇄  [Русский ▾]          Сервис: [Google ▾]       │
├─────────────────────────────┬─────────────────────────────┤
│ Исходный текст              │ Перевод (только чтение)     │
│                             │                             │
├─────────────────────────────┴─────────────────────────────┤
│ Определён: английский   [Перевести Ctrl+Enter] [Копировать]│
└───────────────────────────────────────────────────────────┘
```

- Два `QPlainTextEdit`: исходный текст и перевод (read-only).
- Автоперевод при наборе с задержкой `autoTranslateDelayMs` (0 — выкл.); «Перевести» и Ctrl+Enter —
  перевести сразу. Повторное нажатие до ответа заменяет запрос (канал `MainWindow`, §4.9).
- ⇄ меняет языки местами, переносит перевод в поле исходного текста и сразу переводит его. Если
  исходный язык «авто», новым целевым становится определённый язык последнего перевода; пока он
  неизвестен, кнопка недоступна.
- Строка состояния: «Translating…», определённый язык (когда исходный — «авто»), ошибки (красным,
  текст из `ui/translationerrortext`). «Копировать» доступна, когда есть перевод.
- Выбор сервиса показывает активный сервис и доступен, только когда сервисов больше одного (этап 6).
- Окно не знает о сервисах и сети: испускает `translateRequested(request)`, а `AppController`
  вызывает `TranslationManager` и возвращает итог через `showTranslationStarted()` /
  `showResult(result)`; результат показывается, только если это текущий запрос окна.
- `translateText(text)` подставляет текст, полученный сценарием Ctrl+W, в поле исходного текста,
  ставит курсор в начало и запускает перевод.
- `showTranslation(request, result)` показывает перевод, переданный из всплывающего окна: выставляет
  языки, оба текста и статус, но не запускает новый запрос.
- `closeEvent` скрывает окно в трей; выход — только из меню трея.
- Геометрия сохраняется в настройках.

#### `LanguageComboBox`

`QComboBox` с внутренними кодами языков: `setLanguages(codes, includeAuto)` (сортировка по
названию, «Auto-detect» сверху, текущий язык сохраняется, если он остался в списке),
`currentCode()` / `setCurrentCode()`, сигнал `currentCodeChanged`. Подстановка ближайшего языка
при смене сервиса (§4.8) добавляется на этапе 6.

#### `SettingsDialog`

Модальный диалог редактирует копию `AppSettings`; «ОК» возвращает её, `AppController` сохраняет и
применяет изменения, «Отмена» ничего не меняет. Вкладки:

1. **Общие** — автозагрузка (с подсказкой, если Windows отключила её в Диспетчере задач), запуск
   свёрнутым, язык интерфейса (системный/English/Русский), восстановление буфера обмена, закрытие
   popup при потере фокуса.
2. **Горячие клавиши** — `QKeySequenceEdit` для Ctrl+Q, Ctrl+Shift+Q (перевод с заменой
   выделения), Ctrl+W и показа главного окна. Пустое поле отключает действие. Перед принятием
   проверяются односоставность, поддержка Win32 и отсутствие дубликатов. Пока фокус в любом поле,
   диалог испускает `hotkeyEditingChanged(true)`, и `AppController` приостанавливает глобальные
   клавиши; после ухода фокуса/закрытия — возобновляет.
3. **Перевод** — исходный язык (с «Auto-detect»), основной и дополнительный языки, максимальная
   длина, задержка автоперевода. Дополнительный язык зарезервирован под этап 8.
4. **Сервисы** — Google: режим (бесплатный/Cloud API), ключ (password echo). DeepL: ключ
   (password echo), вариант английского, кнопка «Проверить ключ» с асинхронным результатом
   (`character_count`/`character_limit` или ошибка). Кнопка проверки не блокирует диалог; ключ
   берётся из поля ввода. Ключи сохраняются через DPAPI из этапа 5.
5. **Сеть** — режим (без прокси/системный/ручной), тип HTTP/SOCKS5, хост, порт, авторизация,
   логин, пароль (password echo), таймаут, кнопка «Проверить соединение» с асинхронным результатом.
   Поля включаются только для ручного режима; диалог испускает `proxyTestRequested()`, а
   `AppController` проверяет ещё не сохранённые параметры через `NetworkManager::testProxy`.

#### `AppIcon`

Иконки для трея и окна. Для трея — отдельные варианты под светлую и тёмную тему панели задач
(`QStyleHints::colorScheme()`), во время запроса можно показывать анимированный индикатор.
Иконка exe (`resources/icons/app.ico`) и версия встраиваются через `resources/qtranslate.rc`.

#### Локализация

Строки помечены `tr()`/`QT_TRANSLATE_NOOP`; `i18n/qtranslate_ru_RU.ts` содержит русский перевод и
встраивается `qt_add_translations`. `AppController::applyUiLanguage()` выбирает
системный/English/Русский, ставит `QTranslator` и рассылает `LanguageChange`; главное окно
переприменяет статические тексты, трей перестраивает меню, popup пересоздаётся при следующем
вызове. Английский — исходный язык, русский — перевод.

### 4.14. Переносимость и план поддержки Linux

Windows-код изолирован: заголовки `platform/*.h` не включают `<windows.h>`, а реализации —
`*_win.cpp`; для остальных платформ есть `*_stub.cpp`. `qtranslate_core`, `net/`, `services/` и
UI не используют WinAPI. Благодаря этому `CMakeLists.txt` рассчитан на сборку каркаса под Linux,
но фактическая проверка и реальные платформенные действия появятся только на этапе 11:
сейчас все Linux-реализации — заглушки.

| Модуль | Windows (сейчас) | Linux X11 (план) | Linux Wayland (план) |
|---|---|---|---|
| Горячие клавиши | `RegisterHotKey` | `XGrabKey` через xcb/Xlib | портал `org.freedesktop.portal.GlobalShortcuts`; если нет — команды CLI, привязанные в DE |
| Захват выделения | `SendInput` Ctrl+C | XTest + `QClipboard` | синтетический ввод недоступен; пользователь копирует сам |
| Вставка перевода | `SendInput` Ctrl+V | XTest Ctrl+V | недоступна надёжно; максимум — `wtype`/`ydotool` как внешняя зависимость |
| Активное окно | `GetForegroundWindow` | `_NET_ACTIVE_WINDOW`/`XGetInputFocus` | нет надёжного API; режим «в поле» не гарантируется |
| Автозагрузка | реестр `Run` | `~/.config/autostart/qtranslate.desktop` | то же |
| Секреты | DPAPI | libsecret | libsecret; fallback — файл 0600 + предупреждение |
| Трей | `QSystemTrayIcon`/WinAPI | StatusNotifierItem | StatusNotifierItem; в GNOME нужен AppIndicator-extension |
| Настройки | `%APPDATA%\qtranslate` | `$XDG_CONFIG_HOME/qtranslate` | то же |
| Логи | `%LOCALAPPDATA%\qtranslate` | `$XDG_STATE_HOME/qtranslate` | то же |

**Команды CLI.** Чтобы горячие клавиши работали в любом окружении (и на Wayland, где портала
может не быть), приложение получит команды `--translate-popup`, `--translate-main` и
`--translate-inplace`. Их можно назначить на сочетания в настройках GNOME/KDE/Xfce; для
X11-реализации они не нужны, но остаются удобным fallback и используются тестами.

**Трей.** Qt использует StatusNotifierItem/DBus. В GNOME по умолчанию иконки приложений не
показываются — потребуется расширение AppIndicator; если трей недоступен, приложение должно
показать главное окно и предупредить, а не завершаться молча.

**Секреты.** Предпочтительный вариант — libsecret (`secret_password_store_sync`), который
использует системный keyring. Если библиотеки нет при сборке, секреты пишутся в INI с правами
0600, а в настройках показывается предупреждение. Молчаливое хранение паролей в открытом виде
недопустимо.

**Сборка и пакеты.** CMake уже умеет `install` и `qt_generate_deploy_app_script`; для Linux
добавляются `.desktop`, иконки hicolor, `share/applications` и `RPATH`/`$ORIGIN` для AppImage.
Планируемые артефакты: `qtranslate-<версия>-linux-x86_64.tar.gz` (портативный, с Qt),
`.deb` (зависимости Qt из дистрибутива) и AppImage. CI — `ubuntu-24.04`, Qt 6.5+, тесты в
`xvfb-run`; платформенные тесты Windows (`tst_hotkeymanager`, `tst_autostart`,
`tst_secretprotector`) заменяются Linux-аналогами, остальные 14 тестов общие.

**Ограничения Wayland.** Глобальные сочетания и синтетический ввод — не базовые возможности
Wayland. Портал `GlobalShortcuts` поддерживается не всеми рабочими столами и версиями; вставка
в активное поле и Ctrl+Shift+Q на Wayland не могут быть гарантированы без внешних инструментов
или портала RemoteDesktop. Поэтому на первом этапе Linux-поддержки X11 считается полноценным
таргетом, а Wayland — ограниченным: Ctrl+Q с копированием вручную, CLI-команды и явные
предупреждения в интерфейсе. macOS не планируется.

---

## 5. Основные сценарии

### 5.1. Запуск

```
main
 ├─ QApplication, setQuitOnLastWindowClosed(false)
 ├─ SingleInstance: уже запущены? → отправить "activate" → exit(0)
 ├─ SettingsStore: DPAPI SecretCodec, load()   (миграция, расшифровка секретов)
 ├─ QTranslator по general/uiLanguage
 └─ AppController
     ├─ NetworkManager (timeout, applyProxy), GoogleTranslateService + DeepLService → TranslationManager
     ├─ языки/сервис/maxLength/restoreClipboard из настроек
     ├─ MainWindow + восстановление геометрии; PopupWindow — лениво при первом Ctrl+Q
     ├─ TrayIcon.show()
     ├─ HotkeyManager.register(...)  → занятые сочетания → уведомление в трее
     ├─ Autostart::sync(settings.autostart)
     └─ если нет --minimized и не startMinimized → MainWindow.show()
```

Всё, что не нужно для появления значка в трее, создаётся лениво (окно настроек, всплывающее окно).

### 5.2. Ctrl+Q — перевод во всплывающем окне

```
Пользователь      HotkeyMgr     AppController     SelectionGrabber   TranslationMgr   PopupWindow
    │ Ctrl+Q          │                │                  │                 │               │
    ├────────────────►│ WM_HOTKEY      │                  │                 │               │
    │                 ├─activated────► │ pos = cursor     │                 │               │
    │                 │                ├─grab()─────────► │ ждём отпускания │               │
    │                 │                │                  │ снимок буфера   │               │
    │                 │                │                  │ SendInput Ctrl+C│               │
    │                 │                │                  │ ждём seq number │               │
    │                 │                │                  │ читаем, восстан.│               │
    │                 │                │◄──cb(text)───────┤                 │               │
    │                 │                │ text пуст? → showNoSelection(pos) ─────────────────► │
    │                 │                ├─showLoading(pos)────────────────────────────────────►│
    │                 │                ├─translate(req, Popup)───────────► │ отменить старый│
    │                 │                │                  │                 │ кэш? → сразу   │
    │                 │                │                  │                 │ сервис → HTTP  │
    │                 │                │◄────────────── reply.finished ────┤                 │
    │                 │                ├─showResult / showError────────────────────────────► │
```

### 5.3. Ctrl+W — перевод в главном окне

Отличается от Ctrl+Q:

- `AppController` сначала запускает `SelectionGrabber::grab()`, **не показывая окно**: если показать
  и активировать его раньше, Ctrl+C уйдёт в собственное окно, а не в программу с выделением.
- После колбэка окно показывается и активируется, исходный текст подставляется в левое поле,
  перевод — в правое. Перевод стартует сразу, не дожидаясь показа: окно появляется ещё до ответа
  сервиса.
- Если выделения нет, окно просто открывается с фокусом в поле ввода — это удобный способ
  «вызвать переводчик и набрать текст».
- Повторное срабатывание Ctrl+W во время захвата игнорируется, чтобы не перехватить фокус у
  source-программы.

### 5.4. Ctrl+Shift+Q — перевод с заменой выделения

```
Пользователь   HotkeyMgr   AppController   SelectionGrabber   TranslationMgr   Windows
  │Ctrl+Shift+Q │              │                 │                │              │
  ├────────────►│ WM_HOTKEY    │                 │                │              │
  │             ├─activated──► │ target = foregroundWindow()      │              │
  │             │              ├─grab()──────────►│ снимок буфера, Ctrl+C,       │
  │             │              │◄──text───────────┤ восстановление буфера        │
  │             │              ├─translate(req, InPlace)──────────►│              │
  │             │              │◄──────────── finished ───────────┤              │
  │             │              ├─target всё ещё активен? ────────────────────────►│
  │             │              │   да: буфер := перевод, SendInput Ctrl+V,        │
  │             │              │       через 500 мс вернуть буфер                 │
  │             │              │   нет: перевод в буфер + уведомление в трее      │
```

- Целевое окно (`GetForegroundWindow`) запоминается **до** захвата. Своё окно не показывается,
  поэтому фокус и выделение остаются в поле ввода; после ответа Ctrl+V заменяет выделение.
- Перевод идёт через тот же `TranslationManager`, что и Ctrl+Q, с тем же умным выбором языка:
  при основном русском и дополнительном английском русский ответ автоматически переводится на
  английский. Канал `InPlace` не отменяет запросы popup и главного окна.
- Если пользователь переключился в другое окно, пока шёл запрос, перевод **не вставляется**:
  он копируется в буфер обмена, а в трее показывается уведомление. Это защищает от вставки в
  чужое поле (в том числе в поле пароля).
- Буфер обмена восстанавливается через 500 мс после Ctrl+V. Если `app/restoreClipboard` выключен,
  перевод остаётся в буфере.
- Нет выделения или ошибка перевода → уведомление в трее; окно не появляется.

### 5.5. Применение настроек

```
SettingsDialog ──OK──► SettingsStore::save ──changed(old,new)──► AppController
                                                                    ├─ hotkeys изменились → register/unregister только их
                                                                    ├─ autostart         → Autostart::sync
                                                                    ├─ языки/сервис      → MainWindow/PopupWindow + TranslationManager
                                                                    ├─ maxLength         → TranslationManager + SelectionGrabber
                                                                    ├─ restoreClipboard  → SelectionGrabber
                                                                    ├─ network           → NetworkManager.applyProxy / setTimeoutMs
                                                                    ├─ popup width/focus → PopupWindow
                                                                    ├─ uiLanguage       → QTranslator + LanguageChange
                                                                    └─ service/ключи     → service
```

Пока фокус находится в любом `QKeySequenceEdit`, `SettingsDialog` испускает
`hotkeyEditingChanged(true)`, и `AppController` приостанавливает глобальные клавиши
(`HotkeyManager::setSuspended`). После ухода фокуса или закрытия диалога регистрация
восстанавливается.

Смена сервиса в трее или главном окне вызывает `TranslationManager::setActiveService()`.
`activeServiceChanged` → `AppController::updateLanguageConfiguration()`: списки языков обоих окон
обновляются, неподдерживаемый язык заменяется через `Languages::closest`; выбор сохраняется в
`translation/service` через `SettingsStore::save()`.

### 5.6. Завершение

«Выход» в трее → `AppController::prepareForQuit()`: флаг завершения (чтобы `closeEvent` не прятал
окно и `changed` не применялся повторно), сохранение геометрии главного окна в `SettingsStore`,
`HotkeyManager::unregisterAll()`, отмена активных запросов, скрытие popup → `QApplication::quit()`.
Повторный вызов (`aboutToQuit`, завершение сеанса) ничего не делает.

---

## 6. Обработка ошибок

| Ситуация | Где обрабатывается | Что видит пользователь |
|---|---|---|
| Горячая клавиша занята | `AppController` при регистрации | Уведомление в трее, предупреждение в настройках |
| Нет выделения | `AppController` | «Ничего не выделено» у курсора / пустое главное окно |
| Нет сети, ошибка прокси, TLS | сервис → `Network` | Текст ошибки и «Повторить» в окне перевода |
| Таймаут | `Timeout` | «Сервис не ответил за N с» |
| Пустой текст | `TranslationManager` до запроса → `EmptyText` | «Нечего переводить» |
| Неверный ключ | `Auth` | «Неверный API-ключ DeepL» + ссылка «Открыть настройки» |
| Квота / частые запросы | `QuotaExceeded`, `RateLimited` | Пояснение и совет сменить сервис |
| Неожиданный формат ответа | парсер → `BadResponse` | «Сервис вернул неожиданный ответ», подробности в лог |
| Сервис не настроен | `TranslationManager` до запроса | «Для DeepL нужен API-ключ» + «Открыть настройки» |

Исключения C++ не используются для управления потоком: ошибки передаются в `TranslationResult`.

---

## 7. Логирование, безопасность, приватность

- Категории `QLoggingCategory`: `qtranslate.app`, `.hotkey`, `.grab`, `.net`, `.translation`
  (`TranslationManager`), `.service.google`, `.service.deepl`, `.settings`, `.autostart`, `.secret`.
  По умолчанию выводятся только предупреждения; подробный лог включается
  переменной окружения `QT_LOGGING_RULES` или флагом `--verbose` (запись в
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`, ротация при 5 МБ в `.1`). Обработчик сообщений
  пишет время, уровень, категорию и текст; каталог создаётся при старте.
- **В лог не пишутся переводимые тексты, ключи и пароли** — только длины, коды ответов, время.
- Все запросы только по HTTPS; ошибки TLS не игнорируются.
- Секреты в INI зашифрованы DPAPI (§4.11).
- В справке и при первом выборе сервиса указать, что текст отправляется стороннему сервису
  (Google/DeepL).

---

## 8. Тестирование

Модульные тесты (Qt Test; логика берётся из `qtranslate_core`, платформенные модули
подключаются к отдельным тестовым целям):

| Тест | Что проверяет |
|---|---|
| `tst_googleparser` | Разбор ответов gtx и Cloud API по фикстурам: несколько предложений, определение языка, пустой/битый ответ, коды ошибок Cloud с телом |
| `tst_googletranslateservice` | Сервис против локального HTTP-сервера: free/cloud режимы, тело/ключ, разбиение длинного текста и склейка, HTTP-ошибки, отказ в соединении, таймаут, отмена |
| `tst_deeplparser` | Разбор `translations[]`, почастный разбор, `/v2/usage`, коды ошибок → `TranslationError` |
| `tst_deeplservice` | DeepL против локального HTTP-сервера: Authorization, массив `text`, варианты английского, Free/Pro хосты, ошибки, таймаут, проверка ключа |
| `tst_languages` | Маппинг кодов в обе стороны, варианты `EN-US/EN-GB`, `PT-BR`, `closest` при смене сервиса, неизвестные коды |
| `tst_networkmanager` | Типы прокси, HTTP-прокси с авторизацией и `CONNECT`, SOCKS5 с логином/паролем, `testProxy`, понятные ошибки и отсутствие пароля в тексте |
| `tst_popupplacement` | Позиционирование окна Ctrl+Q: отражение у краёв, прижатие к экрану, второй монитор с отрицательными координатами |
| `tst_popupwindow` | Состояния всплывающего окна (offscreen): запрос, результат, копирование, ошибка/«Повторить», смена языка, закрепление, автозакрытие подсказки |
| `tst_textchunker` | Разбиение по абзацам/предложениям, слова длиннее лимита, суррогатные пары не разрываются |
| `tst_hotkeysequence` | `QKeySequence` ↔ `(MOD, VK)`, неподдерживаемые клавиши |
| `tst_hotkeymanager` | `RegisterHotKey`: конфликт сочетаний, `setSuspended`, отложенная регистрация (Windows; собирается вместе с `platform/hotkeymanager_win.cpp`) |
| `tst_settings` | Значения по умолчанию, запись/чтение, миграция версий, портативный путь, DPAPI-совместимое шифрование секретов |
| `tst_settingsdialog` | Заполнение вкладок (включая ключи/режим/вариант), возврат изменённой копии, проверка сочетаний и языков, приостановка глобальных клавиш, запрос проверки ключа (offscreen) |
| `tst_secretprotector` | DPAPI: round-trip, пустые и испорченные данные |
| `tst_autostart` | Формат команды автозагрузки |
| `tst_translationmanager` | Отмена запросов в канале, кэш, повтор на дополнительный язык — на фейковом сервисе |

Сервисы тестируются через подмену сети: фейковый `TranslationService` для менеджера и
локальный HTTP-сервер (`QTcpServer`) с заранее заданными ответами для проверки кодов ошибок,
таймаутов и прокси.

Ручной чек-лист перед релизом: захват выделения в Блокноте, браузере, Word, VS Code, Telegram;
окно в администраторском процессе (ожидаемо не работает); буфер обмена после перевода совпадает с
исходным; Ctrl+Q у краёв экрана и на двух мониторах с разным масштабом; Ctrl+Shift+Q в поле ввода
(замена выделения, восстановление буфера, защита при смене окна); работа через HTTP- и
SOCKS5-прокси с авторизацией; автозапуск после перезагрузки.

### 8.1. Результаты финальной проверки (этап 8)

Проверено вручную на Release (Windows 11, один монитор 3840×2160, масштаб 175%):

- язык интерфейса переключается English ↔ Русский в настройках без перезапуска: главное окно,
  трей и его меню обновляются сразу, выбор сохраняется в INI;
- умный язык на реальном Google: русский текст при целевом «русский» автоматически переводится на
  английский (в логе два запроса `ru → ru` и `ru → en`), статус «Detected language: Russian»;
- кэш: повторный перевод того же текста занимает ~3 мс, в логе `Cache hit`, новых сетевых запросов
  нет; ошибки не кэшируются (три ответа 429 подряд не создали записей);
- DPI: `GetProcessDpiAwareness` = 2 (per-monitor), контекст окна сравнивается с
  `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`; иконка exe и `WM_GETICON` окна совпадают с
  `resources/icons/app.ico`;
- системный режим и ручной HTTP-прокси (реальный прокси `127.0.0.1:2080`): перевод проходит;
- Ctrl+Q в Блокноте и Edge: окно показывает перевод, буфер обмена восстанавливается;
  Ctrl+W переносит исходник и перевод в главное окно, буфер также восстанавливается;
- Ctrl+Shift+Q в textarea Edge: выделенный русский текст заменён английским переводом, буфер
  обмена восстановлен; при переключении в другое окно во время запроса вставки не произошло,
  перевод остался в буфере и было показано уведомление;
- Ctrl+Q у правого-нижнего края экрана остаётся в рабочей области.

Ограничения среды: Word и VS Code не установлены; Telegram установлен и авторизован, но выделение
в личных чатах не проверялось; второго монитора с другим масштабом нет; чистая Windows без Qt
недоступна (проверен запуск `build/dist/bin/qtranslate.exe` без Qt в `PATH`); реальная перезагрузка
для автозагрузки и прокси с авторизацией не проверялись; см. §9.2.

### 8.2. CI и проверка релиза 0.9.0

Локальная проверка (Windows 11, Qt 6.12.0 MSVC 2022 x64):

- Debug и Release: сборка без предупреждений `/W4`, **17/17 тестов**;
- `package.ps1` собрал `qtranslate-0.9.0-win64.zip` (24,0 МБ),
  `qtranslate-0.9.0-win64-setup.exe` (17,9 МБ) и `SHA256SUMS.txt`; обе контрольные суммы
  совпали с файлами;
- установщик: тихая установка в отдельный каталог, `FileVersion` exe = 0.9.0, ярлык в меню
  «Пуск» создан; запуск `bin\qtranslate.exe` без Qt в `PATH` успешен, `Qt6Core.dll` и
  `vcruntime140.dll` загрузились из каталога установки;
- `AppMutex`: при запущенном qtranslate деинсталлятор завершился с кодом 1 и не удалил файлы;
- удаление: exe и ярлык удалены, каталог установки очищен, `%APPDATA%\qtranslate\qtranslate.ini`
  сохранён;
- портативный zip: распакован в отдельный каталог, `bin\qtranslate.exe` запустился без Qt в
  `PATH`, Qt и CRT загрузились из архива.

CI: workflow `.github/workflows/build.yml` собирает и тестирует Release на `windows-latest`,
затем повторяет упаковку; по тегу `v0.9.0` публикует релиз. Результат прогона и ссылка на релиз
фиксируются в `doc/plan.md` (этап 10).

---

## 9. Принятые решения и развитие

### 9.1. Принятые решения

| Вопрос | Решение |
|---|---|
| Ctrl+Q / Ctrl+W перехватываются у всех программ | Оставить по умолчанию, без предупреждений; меняются в настройках (§4.3) |
| Ctrl+Alt на не-английской раскладке | Qt может вернуть `GroupSwitchModifier` (AltGr); нормализуем его в `MOD_CONTROL \| MOD_ALT`, иначе сочетание ошибочно отвергалось бы (§4.3) |
| HTTPS-прокси | HTTP-прокси с туннелем `CONNECT`; TLS до прокси не поддерживается (§4.10) |
| Окна процессов, запущенных от администратора | Захват выделения в них не работает — это известное ограничение, его не обходим (§4.4) |
| Закрепление 📌 во всплывающем окне | Действует до закрытия окна; новый Ctrl+Q начинает с незакреплённого состояния (§4.13) |
| HTTP/2 и бесплатный Google | Принудительно HTTP/1.1: gtx стабильно отвечал `429` на HTTP/2-запросы Qt, по HTTP/1.1 работает (§4.10) |
| Перевод с заменой выделения | Отдельное действие Ctrl+Shift+Q, не меняет Ctrl+Q. Вставка только если исходное окно всё ещё активно; иначе перевод копируется в буфер и показывается уведомление (§5.4) |
| Установщик | Inno Setup 6.3+/7, установка **для текущего пользователя** без UAC в `%LOCALAPPDATA%\Programs\qtranslate`; настройки при удалении сохраняются (§3.4) |
| Портативная сборка | Остаётся вторым артефактом релиза: zip с `bin/`, `plugins/`, `translations/`; нужна пользователям без прав установки (§3.4) |
| CI и релизы | GitHub Actions на `windows-latest`; push/PR собирают и тестируют, тег `v*` публикует установщик, zip и `SHA256SUMS.txt`; версия тега сверяется с CMake (§3.4) |
| Linux | Отдельный этап 11 после перехода на Linux-машину: X11 — полноценный таргет, Wayland — портал/CLI-fallback с явными ограничениями; macOS не планируется (§4.14) |
| Запись в журнале Win+V при захвате | Промежуточный Ctrl+C может оставить запись; exclusion-форматы защищают только восстановление буфера. Полный обход — стратегия через UI Automation (§9.2), принята как будущее развитие |

### 9.2. Ограничения и возможное развитие

Ограничения среды, в которой выполнялась финальная проверка (не дефекты приложения):

- Word и VS Code не установлены — захват выделения в них не проверен; Telegram авторизован, но
  личные чаты не открывались.
- Второй монитор с другим масштабом отсутствует; проверен один монитор 3840×2160 при 175%.
- Чистая Windows без Qt недоступна; проверен запуск самодостаточного `build/dist` без Qt в `PATH`.
- Реальная перезагрузка Windows для автозагрузки и внешний прокси с авторизацией не проверялись
  (локальные HTTP/SOCKS5-прокси с авторизацией покрыты `tst_networkmanager`).
- Захват в окнах администраторских процессов и запись Win+V — известные ограничения (§9.1).
- Установщик не подписан кодом: Windows SmartScreen может показать предупреждение. Сертификата
  в проекте нет; добавление подписи не меняет формат артефактов.
- Linux-сборка в 0.9.0 отсутствует: платформенные модули — заглушки, полноценный порт — этап 11
  (§4.14). macOS не планируется.

Возможное развитие:

1. **Захват через UI Automation** без использования буфера обмена (§4.4).
2. Словарные статьи (варианты перевода, части речи) — Google gtx отдаёт их при `dt=bd`.
3. Дополнительные сервисы (Yandex, Microsoft, LibreTranslate) — новый класс `TranslationService`.
4. Озвучивание текста (`QTextToSpeech`), история переводов.
5. **Linux** — этап 11: X11-реализации `platform/*`, Wayland через портал `GlobalShortcuts` и
   CLI-fallback, пакеты `.deb`/AppImage (§4.14).
6. Кодовая подпись установщика и автообновление поверх установленной версии.

---

## 10. Порядок реализации

Пошаговый план по этапам — в [`plan.md`](plan.md).

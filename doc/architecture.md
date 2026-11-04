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
| F5 | **Ctrl+W** — перевести выделенный текст в главном окне программы (окно открывается и активируется). |
| F6 | Горячие клавиши настраиваются; Ctrl+Q / Ctrl+W — значения по умолчанию. |
| F7 | Сервисы перевода: Google Translate и DeepL; активный сервис выбирается в настройках и в трее. |
| F8 | Сетевые запросы можно пускать через прокси: HTTP (в том числе для https-запросов, через `CONNECT`), SOCKS5 (с авторизацией и без), либо системный прокси. |

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

- C++17, Qt 6.5+ (разработка ведётся на Qt 6.12, MSVC 2022 x64), CMake ≥ 3.19.
- Модули Qt: `Core`, `Gui`, `Widgets`, `Network`. В тестах дополнительно `Test`.
- Целевая платформа — Windows 10/11. Платформенный код изолирован (см. §3.3), чтобы позже можно было
  добавить Linux/macOS, не трогая ядро.
- TLS — штатный Schannel-бэкенд Qt 6 (`qschannelbackend`), OpenSSL не нужен.
- WinAPI: `user32` (горячие клавиши, `SendInput`, буфер обмена), `crypt32` (DPAPI для секретов).

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
├── doc/
│   └── architecture.md
├── resources/
│   ├── qtranslate.qrc          иконки трея, окна
│   ├── icons/                  app.ico, tray-*.png (светлая/тёмная тема)
│   ├── qtranslate.rc           иконка и версия exe
│   └── qtranslate.manifest     DPI awareness, supportedOS
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
│   │   ├── selectiongrabber.h
│   │   ├── selectiongrabber_win.cpp
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
    ├── tst_languages.cpp
    ├── tst_textchunker.cpp
    ├── tst_translationmanager.cpp
    ├── tst_hotkeysequence.cpp
    └── tst_settings.cpp
```

### 3.2. Соответствие текущему `CMakeLists.txt`

Структура §3.1 — целевая: каталоги и файлы появляются и добавляются в цели по мере реализации
этапов. Сейчас:

| Цель | Что входит сейчас (этап 2) |
|---|---|
| `qtranslate_core` | `core/`: `appinfo`, `languages`, `textchunker`, `translationtypes`, `translationservice`, `translationmanager`; `net/networkmanager`; `services/`: `googleparser`, `googletranslateservice` |
| `qtranslate` | `main.cpp`, `app/*`, `ui/*` (`appicon`, `languagecombobox`, `mainwindow`, `translationerrortext`, `trayicon`), `platform/windowactivation.h` + `windowactivation_win.cpp` (на не-Windows — `windowactivation_stub.cpp`) |
| тесты (`tests/CMakeLists.txt`) | `tst_appinfo`, `tst_googleparser`, `tst_googletranslateservice`, `tst_languages`, `tst_textchunker`, `tst_translationmanager`; каждый тест подключается функцией `qtranslate_add_test(<имя>)`, линкуется с `qtranslate_core` и получает путь к `tests/fixtures` в макросе `QTRANSLATE_FIXTURES_DIR` |

### 3.3. Сборка

- `qtranslate_core` — `STATIC`-библиотека из `core/`, `net/`, `services/`; зависит только от
  `Qt::Core` и `Qt::Network`. С ней линкуются и приложение, и тесты.
- `qtranslate` — исполняемый файл (`WIN32`): `app/`, `platform/`, `ui/`, ресурсы; линкуется с
  `qtranslate_core`, `Qt::Widgets`, `user32`, `crypt32`.
- Платформенные файлы подключаются условием `if(WIN32)`. Заголовки в `platform/` не содержат
  `<windows.h>` — WinAPI виден только в `*_win.cpp`.
- Перевод интерфейса: `qt_add_translations(qtranslate TS_FILES i18n/qtranslate_ru_RU.ts)`,
  `.qm` встраиваются в ресурсы.
- `enable_testing()` + `add_subdirectory(tests)`, каждый тест — отдельный `qt_add_executable` и `add_test`.
- Развёртывание — уже настроенный `qt_generate_deploy_app_script` (windeployqt). Проверить, что в
  дистрибутив попадает плагин `tls/qschannelbackend.dll`.

---

## 4. Модули

### 4.1. `main.cpp` и `SingleInstance`

`main()`:

1. Создаёт `QApplication`, задаёт `applicationName`/`applicationVersion`,
   `setQuitOnLastWindowClosed(false)` — иначе скрытие последнего окна завершит процесс.
   `organizationName` намеренно не задаётся, чтобы данные лежали в `%APPDATA%\qtranslate`
   (см. §4.11).
2. Разбирает аргументы (`QCommandLineParser`): `--minimized` (передаётся из автозагрузки).
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
enum class HotkeyAction { TranslateToPopup, TranslateToMainWindow, ShowMainWindow };

class HotkeyManager : public QObject {
    Q_OBJECT
public:
    // false — сочетание занято другой программой или не поддерживается
    bool registerHotkey(HotkeyAction action, const QKeySequence &seq);
    void unregisterHotkey(HotkeyAction action);
    void unregisterAll();
    // Временно отключить всё (пока пользователь вводит новое сочетание в настройках)
    void setSuspended(bool suspended);
signals:
    void activated(HotkeyAction action);
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
  а `AppController` показывает уведомление в трее с предложением сменить сочетание.

Настройки по умолчанию:

| Действие | По умолчанию |
|---|---|
| `TranslateToPopup` | `Ctrl+Q` |
| `TranslateToMainWindow` | `Ctrl+W` |
| `ShowMainWindow` | не назначено |

> **Решение.** Глобальная регистрация перехватывает сочетание у **всех** программ: Ctrl+W перестанет
> закрывать вкладки в браузере и редакторах, Ctrl+Q — закрывать программы. Владелец проекта эти
> сочетания для закрытия не использует, поэтому Ctrl+Q / Ctrl+W остаются значениями по умолчанию,
> без предупреждений и мастера первого запуска. Сменить их можно в настройках. Срабатывание «только
> при наличии выделения» невозможно: в момент нажатия неизвестно, есть ли выделение.

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
};
```

Алгоритм (конечный автомат на `QTimer`, без блокировок):

1. **Ожидание отпускания клавиш.** Пользователь ещё держит Ctrl и Q. Опрашиваем `GetAsyncKeyState`
   каждые 10 мс, пока не отпущены клавиши сочетания, но не дольше 300 мс. Если Alt/Shift/Win всё ещё
   нажаты, отправляем для них синтетическое отпускание, иначе приложение получит Ctrl+Alt+C и т.п.
2. **Снимок буфера.** Копируем все форматы текущего `QMimeData` (только если включена настройка
   «восстанавливать буфер обмена», по умолчанию включена). Запоминаем `GetClipboardSequenceNumber()`.
3. **Ctrl+C.** `SendInput` с последовательностью Ctrl↓ C↓ C↑ Ctrl↑.
4. **Ожидание изменения.** Каждые 10 мс сравниваем `GetClipboardSequenceNumber()` с запомненным,
   максимум 400 мс. Не изменился — выделения нет, `cb("")`.
5. **Чтение.** `QGuiApplication::clipboard()->text()`, обрезка пробелов по краям, ограничение длины
   (настройка `translation/maxLength`, по умолчанию 20 000 символов).
6. **Восстановление.** Возвращаем сохранённый снимок. К восстановленным данным добавляем форматы
   `ExcludeClipboardContentFromMonitorProcessing` и `CanIncludeInClipboardHistory = 0`, чтобы
   не засорять журнал буфера (Win+V) и менеджеры буфера обмена.
7. `cb(text)`.

Ограничения (описать в справке):

- **UIPI.** Обычный процесс не может отправить ввод в окно процесса, запущенного от администратора.
  Выделение в таких окнах не захватывается; `SendInput` при этом молча ничего не делает.
- Программы, где Ctrl+C означает другое (терминалы — прерывание), или где копирование отключено.

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
    virtual void applySettings(const AppSettings &s) = 0;   // появится на этапе 5
    // Возвращает объект ответа; владеет им вызывающий (deleteLater после finished)
    virtual TranslationReply *translate(const TranslationRequest &req) = 0;
};
```

- `abort()` сначала завершает ответ с `Cancelled`, а потом вызывает `abortImpl()`. Поэтому ошибка,
  которую сообщит прерванный сетевой запрос, уже ничего не меняет.
- `finished()` не испускается внутри `translate()`, даже если ответ известен сразу (неподдерживаемый
  язык, пустой текст): вызывающий должен успеть подключиться. Для этого есть `createFinished()`.
- Вызывающий может удалить ответ и до `finished()` — это отменяет запрос без сигнала.
- `applySettings()` в интерфейсе пока нет: метод добавляется вместе с `AppSettings` на этапе 5.

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

{"q": ["…"], "target": "ru", "source": "en", "format": "text"}
```

Ответ: `data.translations[].translatedText` и `detectedSourceLanguage`. `source` не передаётся,
если выбран «auto».

Длинный текст режется `TextChunker` на части до 5000 символов (UTF-16) по границам
абзацев/предложений. Пробный POST на ~6900 символов к gtx проходит, но лимит не документирован,
поэтому разбиение делается всегда. Части отправляются последовательно, результат склеивается; при
ошибке любой части запрос завершается с этой ошибкой. Пробелы и переводы строк на краях каждой
части в запрос не попадают и возвращаются на место при склейке — сервис может их не сохранить, а на
стыках частей это сломало бы форматирование. Определённый язык берётся из первой части, где он есть.

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
- `source_lang` не передаётся при «auto».
- Ответ: `translations[].text`, `translations[].detected_source_language`.
- Коды ошибок: 403 → `Auth`, 456 → `QuotaExceeded`, 429 → `RateLimited`, 413 → `TooLong`.
- Лимит тела запроса — 128 КиБ; длинный текст режется `TextChunker` и отправляется массивом `text`.
- В настройках кнопка «Проверить ключ» вызывает `GET /v2/usage` и показывает израсходованный лимит
  символов.

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
- `displayName(code)` — название для UI, помечено `QT_TRANSLATE_NOOP` (контекст `Languages`) под
  локализацию этапа 8; `nativeName(code)` — самоназвание (подсказка в списке);
- `autoCode()` — `"auto"`; `forLocale(QLocale)` / `systemLanguage()` — язык системы с учётом
  письменности и региона (`zh-Hans`/`zh-Hant`, `pt-BR`/`pt-PT`, нюнорск → `nb`), иначе `en`.

Списки языков у сервисов разные (у DeepL их меньше). `LanguageComboBox` показывает только
поддерживаемые выбранным сервисом; если при смене сервиса текущий язык не поддерживается,
выбирается ближайший (например, `pt` → `pt-BR`), иначе — язык по умолчанию.

### 4.9. `TranslationManager`

Фасад над сервисами, с которым работают `AppController` и UI.

```cpp
class TranslationManager : public QObject {
    Q_OBJECT
public:
    enum class Channel { MainWindow, Popup };

    void registerService(TranslationService *service);   // забирает владение; первый — активный
    QList<TranslationService *> services() const;
    TranslationService *service(const QString &id) const;
    TranslationService *activeService() const;
    void setActiveService(const QString &id);
    void setMaxLength(int length);                         // 20 000 по умолчанию; из настроек — этап 5

    // channel — «канал» запроса: Popup или MainWindow.
    // Новый запрос в том же канале отменяет предыдущий незавершённый.
    TranslationReply *translate(const TranslationRequest &req, Channel channel);
    void cancel(Channel channel);
    void cancelAll();                                      // при завершении программы (§5.5)
signals:
    void activeServiceChanged(const QString &id);
};
```

Менеджер хранит на текущий ответ каждого канала только `QPointer`: ответом владеет вызывающий.
Пока кэша и повтора на дополнительный язык нет (этап 8), `translate()` возвращает ответ сервиса как
есть; для кэша и повтора понадобится обёртка над ответом сервиса — интерфейс менеджера от этого не
меняется. Отменённый ответ приходит вызывающему с `Cancelled`, и UI его не показывает.

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
    QNetworkRequest makeRequest(const QUrl &url) const;   // таймаут, User-Agent, HTTP/2
    // Проверка прокси без сохранения: временный QNAM с заданными параметрами
    void testProxy(const ProxySettings &p, std::function<void(bool ok, QString message)> cb);
};
```

Сопоставление с Qt:

| Режим / тип | Реализация |
|---|---|
| Без прокси | `QNetworkProxy::NoProxy` |
| Системный | `QNetworkProxyFactory::setUseSystemConfiguration(true)` (WinINet/WPAD, PAC) |
| HTTP | `QNetworkProxy::HttpProxy`; для https-адресов Qt сам делает туннель `CONNECT` |
| SOCKS5 | `QNetworkProxy::Socks5Proxy`; DNS-имена резолвятся на стороне прокси |

Логин и пароль задаются прямо в `QNetworkProxy`. Сигнал `proxyAuthenticationRequired` при
неверных данных завершает запрос ошибкой `Network` с понятным текстом «прокси отклонил авторизацию»,
а не показывает диалог.

После `applyProxy` вызывается `nam->clearConnectionCache()`, чтобы уже открытые keep-alive
соединения не продолжали идти мимо нового прокси.

**О «HTTPS-прокси» (решение принято).** Под HTTPS-прокси в проекте понимается HTTP-прокси,
через который идут https-запросы (туннель `CONNECT`). Отдельного типа для этого нет: тип `Http`
обслуживает и http-, и https-адреса. Прокси с TLS-соединением *до самого прокси* `QNetworkProxy`
не поддерживает, и в проекте он не нужен.

SOCKS4 Qt не поддерживает; в UI предлагается только SOCKS5.

Общие параметры запросов: `setTransferTimeout(10 000 мс)` (настраивается), постоянный
`User-Agent: qtranslate/<версия>`, только HTTPS.

**Состояние на этапе 2.** Реализованы `nam()`, `makeRequest()` и `setTimeoutMs()` (таймаут из
настроек подключается на этапе 5). Прокси не настраивается совсем: до этапа 7 запросы идут напрямую,
системный прокси Windows не используется (Qt без `setUseSystemConfiguration(true)` его не читает,
переменные окружения `HTTPS_PROXY` и т. п. на Windows тоже не учитываются).

### 4.11. Настройки — `AppSettings` и `SettingsStore`

`AppSettings` — обычная структура-значение со всеми параметрами. `SettingsStore` загружает и
сохраняет её и сообщает об изменениях:

```cpp
class SettingsStore : public QObject {
    Q_OBJECT
public:
    const AppSettings &current() const;
    void save(const AppSettings &s);       // пишет на диск и испускает changed
signals:
    void changed(const AppSettings &oldS, const AppSettings &newS);
};
```

Окно настроек редактирует **копию** `AppSettings`; по «Применить»/«ОК» отдаёт её в `save()`.
`AppController` сравнивает старое и новое значение и применяет только изменившееся
(перерегистрирует клавиши, меняет прокси и т.д.).

Хранение: INI-файл через `QSettings`.

- Обычный режим: `%APPDATA%\qtranslate\qtranslate.ini`.
- Портативный режим: если рядом с exe лежит `qtranslate.ini`, используется он.

Замечание для этапа 5: `organizationName` не задан (§4.1), поэтому конструктор `QSettings` по
умолчанию для этого не годится. `SettingsStore` открывает INI по явному пути —
`QSettings(path, QSettings::IniFormat)`, где `path` — `%APPDATA%\qtranslate\qtranslate.ini`
(или файл рядом с exe в портативном режиме).

Секреты (API-ключи, пароль прокси) хранятся зашифрованными через Windows DPAPI
(`CryptProtectData`, привязка к учётной записи пользователя) в base64. Модуль
`platform/secretprotector` предоставляет `protect(QString) → QByteArray` и
`unprotect(QByteArray) → QString`. Если расшифровать не удалось (файл перенесён на другую машину),
поле считается пустым.

Ключи файла:

| Ключ | Тип | По умолчанию |
|---|---|---|
| `general/autostart` | bool | `false` |
| `general/startMinimized` | bool | `true` |
| `general/uiLanguage` | string | системный (`ru`/`en`) |
| `general/restoreClipboard` | bool | `true` |
| `hotkeys/popup` | QKeySequence (portable text) | `Ctrl+Q` |
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
    bool isEnabled();
    bool setEnabled(bool on);   // false — нет доступа к реестру
    void sync(bool wanted);     // привести реестр к настройке, обновить путь
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
- Создаётся один раз и переиспользуется, чтобы появляться без задержки.
- Позиция: у курсора в момент нажатия клавиши (точка запоминается до захвата выделения), с
  ограничением рамками `QScreen::availableGeometry()` того монитора, где курсор. Высота подстраивается
  под текст до предела, дальше прокрутка.
- Состояния: «загрузка» (спиннер, показ сразу), «результат», «ошибка» (текст и кнопка «Повторить»),
  «нет выделения» (короткая подсказка, закрывается через 1,5 с).
- Закрытие: Esc, ✕, потеря фокуса (если не закреплено 📌 и включена настройка).
- Клик по «EN → RU» меняет язык перевода и сразу переводит заново.
- «В главное окно» переносит исходный текст и перевод в `MainWindow`.
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
- Автоперевод при наборе с задержкой `autoTranslateDelayMs` (этап 8); «Перевести» и Ctrl+Enter —
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
- `closeEvent` скрывает окно в трей; выход — только из меню трея.
- Геометрия сохраняется в настройках.

#### `LanguageComboBox`

`QComboBox` с внутренними кодами языков: `setLanguages(codes, includeAuto)` (сортировка по
названию, «Auto-detect» сверху, текущий язык сохраняется, если он остался в списке),
`currentCode()` / `setCurrentCode()`, сигнал `currentCodeChanged`. Подстановка ближайшего языка
при смене сервиса (§4.8) добавляется на этапе 6.

#### `SettingsDialog`

Вкладки:

1. **Общие** — автозагрузка, запуск свёрнутым, язык интерфейса, восстановление буфера обмена.
2. **Горячие клавиши** — `QKeySequenceEdit` для каждого действия; пока поле в фокусе,
   `HotkeyManager::setSuspended(true)`, чтобы сочетание не перехватывалось глобально. При
   «Применить» показывается, какие сочетания не удалось зарегистрировать.
3. **Перевод** — сервис по умолчанию, исходный язык, основной и дополнительный языки, максимальная
   длина, задержка автоперевода.
4. **Сервисы** — Google: режим (бесплатный/Cloud API), ключ. DeepL: ключ, вариант английского,
   кнопка «Проверить ключ» (показывает остаток лимита).
5. **Сеть** — режим прокси, тип, хост, порт, авторизация, таймаут, кнопка «Проверить соединение»
   (`NetworkManager::testProxy` — запрос к сервису через ещё не сохранённые параметры).

#### `AppIcon`

Иконки для трея и окна. Для трея — отдельные варианты под светлую и тёмную тему панели задач
(`QStyleHints::colorScheme()`), во время запроса можно показывать анимированный индикатор.

---

## 5. Основные сценарии

### 5.1. Запуск

```
main
 ├─ QApplication, setQuitOnLastWindowClosed(false)
 ├─ SingleInstance: уже запущены? → отправить "activate" → exit(0)
 ├─ SettingsStore::load()          (миграция, расшифровка секретов)
 ├─ установить QTranslator
 └─ AppController
     ├─ NetworkManager.applyProxy(settings.proxy)
     ├─ GoogleTranslateService, DeepLService → TranslationManager
     ├─ TrayIcon.show()
     ├─ HotkeyManager.register(...)  → занятые сочетания → уведомление в трее
     ├─ Autostart::sync(settings.autostart)
     ├─ MainWindow создаётся скрытым; PopupWindow — лениво при первом Ctrl+Q
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

- результат выводится в `MainWindow` (исходный текст подставляется в левое поле, перевод — в правое);
- окно показывается и активируется сразу, не дожидаясь перевода;
- если выделения нет, окно просто открывается с фокусом в поле ввода — это удобный способ
  «вызвать переводчик и набрать текст».

### 5.4. Применение настроек

```
SettingsDialog ──save(copy)──► SettingsStore ──changed(old,new)──► AppController
                                                                    ├─ hotkeys изменились → unregisterAll + register
                                                                    ├─ proxy изменился   → NetworkManager.applyProxy
                                                                    ├─ autostart         → Autostart::setEnabled
                                                                    ├─ service/ключи     → service.applySettings,
                                                                    │                      TranslationManager.setActiveService
                                                                    └─ uiLanguage        → перезагрузить QTranslator
                                                                                           (окна обрабатывают LanguageChange)
```

### 5.5. Завершение

«Выход» в трее → `AppController::shutdown()`: `HotkeyManager::unregisterAll()`, отмена активных
запросов, сохранение геометрии окон → `QApplication::quit()`.

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
  (`TranslationManager`), `.service.google`, `.service.deepl`. По умолчанию выводятся только предупреждения; подробный лог включается
  переменной окружения `QT_LOGGING_RULES` или флагом `--verbose` (запись в
  `%LOCALAPPDATA%\qtranslate\qtranslate.log`).
- **В лог не пишутся переводимые тексты, ключи и пароли** — только длины, коды ответов, время.
- Все запросы только по HTTPS; ошибки TLS не игнорируются.
- Секреты в INI зашифрованы DPAPI (§4.11).
- В справке и при первом выборе сервиса указать, что текст отправляется стороннему сервису
  (Google/DeepL).

---

## 8. Тестирование

Модульные тесты (Qt Test, библиотека `qtranslate_core`, без сети и без WinAPI):

| Тест | Что проверяет |
|---|---|
| `tst_googleparser` | Разбор ответов gtx и Cloud API по фикстурам: несколько предложений, определение языка, пустой/битый ответ |
| `tst_googletranslateservice` | Сервис против локального HTTP-сервера: тело запроса, разбиение длинного текста и склейка, HTTP-ошибки, отказ в соединении, таймаут, отмена |
| `tst_deeplparser` | Разбор `translations[]`, коды ошибок → `TranslationError` |
| `tst_languages` | Маппинг кодов в обе стороны, варианты `EN-US/EN-GB`, `PT-BR`, неизвестные коды |
| `tst_textchunker` | Разбиение по абзацам/предложениям, слова длиннее лимита, суррогатные пары не разрываются |
| `tst_hotkeysequence` | `QKeySequence` ↔ `(MOD, VK)`, неподдерживаемые клавиши |
| `tst_settings` | Значения по умолчанию, запись/чтение, миграция версий |
| `tst_translationmanager` | Отмена запросов в канале, кэш, повтор на дополнительный язык — на фейковом сервисе |

Сервисы тестируются через подмену сети: фейковый `TranslationService` для менеджера и
локальный HTTP-сервер (`QTcpServer`) с заранее заданными ответами для проверки кодов ошибок,
таймаутов и прокси.

Ручной чек-лист перед релизом: захват выделения в Блокноте, браузере, Word, VS Code, Telegram;
окно в администраторском процессе (ожидаемо не работает); буфер обмена после перевода совпадает с
исходным; работа через HTTP- и SOCKS5-прокси с авторизацией; автозапуск после перезагрузки;
два монитора с разным масштабом.

---

## 9. Принятые решения и развитие

### 9.1. Принятые решения

| Вопрос | Решение |
|---|---|
| Ctrl+Q / Ctrl+W перехватываются у всех программ | Оставить по умолчанию, без предупреждений; меняются в настройках (§4.3) |
| HTTPS-прокси | HTTP-прокси с туннелем `CONNECT`; TLS до прокси не поддерживается (§4.10) |
| Окна процессов, запущенных от администратора | Захват выделения в них не работает — это известное ограничение, его не обходим (§4.4) |

### 9.2. Возможное развитие

1. **Захват через UI Automation** без использования буфера обмена (§4.4).
2. Словарные статьи (варианты перевода, части речи) — Google gtx отдаёт их при `dt=bd`.
3. Дополнительные сервисы (Yandex, Microsoft, LibreTranslate) — новый класс `TranslationService`.
4. Озвучивание текста (`QTextToSpeech`), история переводов.
5. Linux/macOS: реализации `platform/*` (X11/Wayland-ограничения на глобальные клавиши, PRIMARY selection).

---

## 10. Порядок реализации

Пошаговый план по этапам — в [`plan.md`](plan.md).

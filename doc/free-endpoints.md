# Бесплатные веб-эндпоинты перевода

Дата: 2026-11-05. Статус: **DeepL web реализован как режим `free-web` сервиса DeepL;
остальные эндпоинты — исследование**.

Документ фиксирует бесплатные эндпоинты без API-ключа, которые можно добавить в qtranslate как
экспериментальные сервисы. Основной источник — открытый проект
[ahatem/QTranslate](https://github.com/ahatem/QTranslate) (MPL-2.0), плагины `deepl-services`,
`yandex-web-services`, `bing-services`, `reverso-services`, `mozhi-services`, `mymemory-services`.

Все перечисленные ниже веб-эндпоинты, кроме MyMemory и self-hosted LibreTranslate/Mozhi,
**неофициальные**: у них нет SLA и квот, они могут измениться, потребовать cookies/капчу,
начать отдавать `429` или блокировать IP. Условия сервисов могут ограничивать автоматическое
использование. DeepL web реализован как переключаемый режим одного сервиса «DeepL» без ключа, но
явно помечен в UI как неофициальный; остальные рискованные эндпоинты лучше включать только явно и
не делать единственным способом перевода.

## 1. DeepL web

- **Запрос:** `POST https://oneshot-free.www.deepl.com/v1/translate`
- **Заголовки:** `Authorization: None`, `Content-Type: application/json`,
  `Accept: application/json`, браузерный `User-Agent`.
- **Тело:** `{"text":["..."],"target_lang":"de"}`; `source_lang` можно не передавать.
- **Ответ:** `{"translations":[{"detected_source_language":"en","text":"...", "...":...}]}`.
- **Ограничения:** неофициальный endpoint; rate limit по IP; ошибка `1042911` / `Too many requests`.
- **Что делает QTranslate:** пробует официальный API при наличии ключа; при отказе ключа один раз
  переходит на веб-endpoint; веб-запросы не чаще раза в 2 секунды; текст режется по 5000 символов;
  на `429` пауза и одна повторная попытка.
- **Проверка:** 2026-11-05 endpoint ответил `200` на нейтральную фразу; до этого был `429`, то есть
  лимит реальный и зависит от IP/частоты.

**Реализация в qtranslate (2026-11-05).**

- Режим `free-web` в настройках сервиса «DeepL», рядом с режимом API, как у Google
  free/Cloud. Отдельного `deepl-web` в списке сервисов больше нет. Режим доступен без ключа,
  поле ключа блокируется, в настройках показывается предупреждение о неофициальности.
  Старые настройки `service=deepl-web` мигрируют в `service=deepl` + `deepl.mode=free-web`.
- Заголовки: `Authorization: None`, `Accept: application/json`, `Content-Type: application/json`;
  `User-Agent` qtranslate подходит, маскироваться под браузер не нужно.
- Коды языков: базовые — в нижнем регистре (`DE` → `de`), региональные — `pt-BR`, `en-US`,
  `zh-Hans`. Важно: `DE` в верхнем регистре эндпоинт принимает за неизвестный код и возвращает
  исходный текст с `200`, поэтому приведение к нижнему регистру обязательно. `PT-BR`, `EN-US` и
  `zh-Hans` (после приведения из `ZH-HANS`) проверены живыми запросами.
- 5000 символов на часть, все части уходят одним запросом массивом `text`; пробелы на краях
  восстанавливаются.
- Внутри `DeepLWebService` запросы сериализуются и идут не чаще раза в 2 с; на `429` — пауза 3 с
  и одна повторная попытка; `403` считается блокировкой/лимитом. `413` → `TooLong`, `5xx` →
  `Network`.
- Код: `src/services/deeplwebservice.*`, `src/services/deeplwebparser.*`; тесты:
  `tst_deeplwebparser`, `tst_deeplwebservice`.

## 2. Yandex web

- **Запрос:** `POST https://api.browser.yandex.com/instaserp/translate`
- **Формат:** `application/x-www-form-urlencoded`; браузерный `User-Agent` (в QTranslate —
  Chrome/YaBrowser).
- **Поля:** `text`, `brandID=int`, `statLang=<target>`, `targetLang=auto`, `locale=<target>`,
  `clid=2270494`, `disable=serp`, `use_llm_srv=0`.
- **Ответ:** JSON-массив, первый элемент содержит `text` и определённый язык.
- **Ограничения:** неофициальный endpoint; QTranslate держит паузу 750 мс и при ошибке пробует
  Mozhi с `engine=yandex`.
- **Зачем:** хорошее покрытие ru/uk/tr и языков СНГ.

## 3. Bing web

- **Подготовка:** `GET https://www.bing.com/translator` и разбор HTML:
  `IG`, `data-iid`, `params_AbusePreventionHelper` (key/token), `MUID`, `SID`, `TID`.
- **Запрос:** `POST https://www.bing.com/ttranslatev3?isVertical=1&IG=...&IID=...`
- **Формат:** form-data: `text`, `fromLang`, `to`, `token`, `key`, `isAuthv2=true`,
  `tryFetchingGenderDebiasedTranslations=true`; cookie `MUID`.
- **Ответ:** JSON с `detectedLanguage` и массивом `translations[].text`.
- **Ограничения:** токен живёт около часа; при `401` нужно один раз обновить токен и повторить,
  без циклов. HTML Bing меняется, поэтому парсер хрупкий.
- **Сложность:** выше остальных из-за извлечения токенов и cookie.

## 4. Reverso

- **Запрос:** `POST https://cps.reverso.net/api2/TranslateText`
- **Тело:** JSON `{"source":"...","direction":"...","deviceId":"0","uiLang":"en",
  "origin":"chromeextension","accessToken":"","appId":"0"}`.
- **Ответ:** `translation`, `directionFrom`, `directionTo`, `success`, `message`.
- **Ограничения:** неофициальный endpoint; пауза 600 мс; коды направлений свои; покрытие языков
  уже, чем у Google/DeepL.

## 5. Mozhi

- **Запрос:** `GET https://<instance>/api/translate?engine=<engine>&from=<src>&to=<dst>&text=...`
- **Ответ:** `"translated-text"`, `detected`, `"source_language"`, `"target_transliteration"`.
- **Движки:** Google, DeepL, DuckDuckGo, LibreTranslate, MyMemory, Reverso, Yandex.
- **Инстансы:** публичные (`mozhi.adminforge.de`, `mozhi.pussthecat.org` и др.) или self-hosted;
  публичные могут быть недоступны, поэтому нужен выбор инстанса и fallback.
- **Плюс:** одна интеграция даёт несколько движков и возможность self-hosted.
- **Минус:** это прокси к чужим endpoint'ам, стабильность и условия зависят от инстанса.

## 6. MyMemory

- **Запрос:** `GET https://api.mymemory.translated.net/get?q=...&langpair=en|ru&mt=1`
- **Ответ:** `responseData.translatedText`, `responseStatus`, `responseDetails`,
  `quotaFinished`.
- **Ограничения:** бесплатный API, но около **500 байт на запрос**, требуется явный исходный
  язык (автоопределение не поддерживается), есть квота; QTranslate держит паузу 350 мс и режет
  текст по 500 байт.
- **Плюс:** не нужен ключ, более предсказуем, чем scraping.
- **Минус:** короткие запросы, качество смешанное, для длинных текстов много запросов.

## 7. DeepL API: актуальный бесплатный тариф

Раньше здесь был указан `DeepL API Free` с 500 000 символов в месяц. Это устарело: сейчас
**DeepL API Developer** даёт **1 000 000 символов всего**, и этот лимит **не сбрасывается
ежемесячно**. После исчерпания нужно переходить на DeepL API Growth. Ключи с суффиксом `:fx`
по-прежнему используют хост `api-free.deepl.com`, но это уже не бесконечный месячный бесплатный
тариф.

Практический вывод: официальный ключ DeepL — фактически trial на 1 млн символов, а не постоянный
бесплатный вариант. Поэтому веб-endpoint DeepL полезен как бесплатный режим или fallback, но
остаётся неофициальным.

## 8. Как встраивать

1. DeepL web — режим `free-web` внутри `DeepLService` (внутренний `DeepLWebService`) и
   предупреждение в настройках; отдельного сервиса нет. Для остальных рискованных эндпоинтов
   можно добавить общий переключатель «Разрешить неофициальные бесплатные сервисы».
2. Для каждого сервиса — свой `TranslationService` и парсер; ключи не нужны.
3. Ограничение частоты на хост: mutex + пауза (DeepL 2 с, Yandex 750 мс, Reverso 600 мс,
   MyMemory 350 мс); без параллельных запросов к одному хосту.
4. Разбиение текста: DeepL 5000 символов, MyMemory 500 байт, остальные — по лимитам сервиса.
5. Ошибки: `429`/`403`/HTML-заглушка → `RateLimited`/`ServiceUnavailable` с понятным текстом,
   без шторма повторных запросов.
6. Fallback: официальный API → бесплатный web → Mozhi/другой сервис; не показывать сырой HTML.
7. Приватность: текст уходит стороннему сервису; в лог не писать текст, ключи и cookies.
8. Тесты: фикстуры ответов и локальный `QTcpServer`; реальные endpoint'ы — только ручной smoke,
   потому что они живые и нестабильные.

## 9. Приоритеты

1. **DeepL web** — реализован: высокое качество, но высокий риск; доступен как режим DeepL.
2. **MyMemory** — самый простой и относительно официальный бесплатный API.
3. **Mozhi** — одна интеграция, много движков, возможен self-hosted.
4. **Yandex web** — важно для русской аудитории.
5. **Bing web** — хрупкая авторизация и HTML; делать после остальных.
6. **Reverso** — нишевый, меньше языков.

## 10. Что не делать

- Не включать неофициальные endpoint'ы по умолчанию и не выдавать их за официальный API.
- Не обходить платные квоты и не публиковать чужие ключи/cookies.
- Не использовать публичные инстансы Mozhi/LibreTranslate как единственный канал без fallback.
- Не копировать код QTranslate без соблюдения MPL-2.0; реализации писать самостоятельно по
  наблюдаемому HTTP-протоколу.

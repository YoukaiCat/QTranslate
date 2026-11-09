#include "core/languages.h"

#include <QCoreApplication>

#include <iterator>

namespace {

struct LanguageInfo
{
    const char *code;        // internal code
    const char *englishName; // translatable, context "Languages"
    const char *nativeName;  // UTF-8
    const char *google;      // nullptr: not supported
    const char *deeplSource;
    const char *deeplTarget;
};

// The DeepL lists follow the API documentation as of 2025; they are revisited
// together with DeepLService (stage 6).
// clang-format off
constexpr LanguageInfo kLanguages[] = {
    {"af",      QT_TRANSLATE_NOOP("Languages", "Afrikaans"),             "Afrikaans",            "af",    nullptr, nullptr},
    {"sq",      QT_TRANSLATE_NOOP("Languages", "Albanian"),              "Shqip",                "sq",    nullptr, nullptr},
    {"am",      QT_TRANSLATE_NOOP("Languages", "Amharic"),               "አማርኛ",                 "am",    nullptr, nullptr},
    {"ar",      QT_TRANSLATE_NOOP("Languages", "Arabic"),                "العربية",              "ar",    "AR",    "AR"},
    {"hy",      QT_TRANSLATE_NOOP("Languages", "Armenian"),              "Հայերեն",              "hy",    nullptr, nullptr},
    {"az",      QT_TRANSLATE_NOOP("Languages", "Azerbaijani"),           "Azərbaycan dili",      "az",    nullptr, nullptr},
    {"eu",      QT_TRANSLATE_NOOP("Languages", "Basque"),                "Euskara",              "eu",    nullptr, nullptr},
    {"be",      QT_TRANSLATE_NOOP("Languages", "Belarusian"),            "Беларуская",           "be",    nullptr, nullptr},
    {"bn",      QT_TRANSLATE_NOOP("Languages", "Bengali"),               "বাংলা",                "bn",    nullptr, nullptr},
    {"bs",      QT_TRANSLATE_NOOP("Languages", "Bosnian"),               "Bosanski",             "bs",    nullptr, nullptr},
    {"bg",      QT_TRANSLATE_NOOP("Languages", "Bulgarian"),             "Български",            "bg",    "BG",    "BG"},
    {"ca",      QT_TRANSLATE_NOOP("Languages", "Catalan"),               "Català",               "ca",    nullptr, nullptr},
    {"ceb",     QT_TRANSLATE_NOOP("Languages", "Cebuano"),               "Cebuano",              "ceb",   nullptr, nullptr},
    {"ny",      QT_TRANSLATE_NOOP("Languages", "Chichewa"),              "Chichewa",             "ny",    nullptr, nullptr},
    {"zh-Hans", QT_TRANSLATE_NOOP("Languages", "Chinese (Simplified)"),  "简体中文",              "zh-CN", "ZH",    "ZH-HANS"},
    {"zh-Hant", QT_TRANSLATE_NOOP("Languages", "Chinese (Traditional)"), "繁體中文",              "zh-TW", "ZH",    "ZH-HANT"},
    {"co",      QT_TRANSLATE_NOOP("Languages", "Corsican"),              "Corsu",                "co",    nullptr, nullptr},
    {"hr",      QT_TRANSLATE_NOOP("Languages", "Croatian"),              "Hrvatski",             "hr",    nullptr, nullptr},
    {"cs",      QT_TRANSLATE_NOOP("Languages", "Czech"),                 "Čeština",              "cs",    "CS",    "CS"},
    {"da",      QT_TRANSLATE_NOOP("Languages", "Danish"),                "Dansk",                "da",    "DA",    "DA"},
    {"nl",      QT_TRANSLATE_NOOP("Languages", "Dutch"),                 "Nederlands",           "nl",    "NL",    "NL"},
    {"en",      QT_TRANSLATE_NOOP("Languages", "English"),               "English",              "en",    "EN",    "EN-US"},
    {"eo",      QT_TRANSLATE_NOOP("Languages", "Esperanto"),             "Esperanto",            "eo",    nullptr, nullptr},
    {"et",      QT_TRANSLATE_NOOP("Languages", "Estonian"),              "Eesti",                "et",    "ET",    "ET"},
    {"fil",     QT_TRANSLATE_NOOP("Languages", "Filipino"),              "Filipino",             "tl",    nullptr, nullptr},
    {"fi",      QT_TRANSLATE_NOOP("Languages", "Finnish"),               "Suomi",                "fi",    "FI",    "FI"},
    {"fr",      QT_TRANSLATE_NOOP("Languages", "French"),                "Français",             "fr",    "FR",    "FR"},
    {"fy",      QT_TRANSLATE_NOOP("Languages", "Frisian"),               "Frysk",                "fy",    nullptr, nullptr},
    {"gl",      QT_TRANSLATE_NOOP("Languages", "Galician"),              "Galego",               "gl",    nullptr, nullptr},
    {"ka",      QT_TRANSLATE_NOOP("Languages", "Georgian"),              "ქართული",              "ka",    nullptr, nullptr},
    {"de",      QT_TRANSLATE_NOOP("Languages", "German"),                "Deutsch",              "de",    "DE",    "DE"},
    {"el",      QT_TRANSLATE_NOOP("Languages", "Greek"),                 "Ελληνικά",             "el",    "EL",    "EL"},
    {"gu",      QT_TRANSLATE_NOOP("Languages", "Gujarati"),              "ગુજરાતી",              "gu",    nullptr, nullptr},
    {"ht",      QT_TRANSLATE_NOOP("Languages", "Haitian Creole"),        "Kreyòl ayisyen",       "ht",    nullptr, nullptr},
    {"ha",      QT_TRANSLATE_NOOP("Languages", "Hausa"),                 "Hausa",                "ha",    nullptr, nullptr},
    {"haw",     QT_TRANSLATE_NOOP("Languages", "Hawaiian"),              "ʻŌlelo Hawaiʻi",       "haw",   nullptr, nullptr},
    {"he",      QT_TRANSLATE_NOOP("Languages", "Hebrew"),                "עברית",                "iw",    "HE",    "HE"},
    {"hi",      QT_TRANSLATE_NOOP("Languages", "Hindi"),                 "हिन्दी",                 "hi",    nullptr, nullptr},
    {"hmn",     QT_TRANSLATE_NOOP("Languages", "Hmong"),                 "Hmoob",                "hmn",   nullptr, nullptr},
    {"hu",      QT_TRANSLATE_NOOP("Languages", "Hungarian"),             "Magyar",               "hu",    "HU",    "HU"},
    {"is",      QT_TRANSLATE_NOOP("Languages", "Icelandic"),             "Íslenska",             "is",    nullptr, nullptr},
    {"ig",      QT_TRANSLATE_NOOP("Languages", "Igbo"),                  "Igbo",                 "ig",    nullptr, nullptr},
    {"id",      QT_TRANSLATE_NOOP("Languages", "Indonesian"),            "Bahasa Indonesia",     "id",    "ID",    "ID"},
    {"ga",      QT_TRANSLATE_NOOP("Languages", "Irish"),                 "Gaeilge",              "ga",    nullptr, nullptr},
    {"it",      QT_TRANSLATE_NOOP("Languages", "Italian"),               "Italiano",             "it",    "IT",    "IT"},
    {"ja",      QT_TRANSLATE_NOOP("Languages", "Japanese"),              "日本語",                "ja",    "JA",    "JA"},
    {"jv",      QT_TRANSLATE_NOOP("Languages", "Javanese"),              "Basa Jawa",            "jw",    nullptr, nullptr},
    {"kn",      QT_TRANSLATE_NOOP("Languages", "Kannada"),               "ಕನ್ನಡ",                 "kn",    nullptr, nullptr},
    {"kk",      QT_TRANSLATE_NOOP("Languages", "Kazakh"),                "Қазақ тілі",           "kk",    nullptr, nullptr},
    {"km",      QT_TRANSLATE_NOOP("Languages", "Khmer"),                 "ខ្មែរ",                  "km",    nullptr, nullptr},
    {"rw",      QT_TRANSLATE_NOOP("Languages", "Kinyarwanda"),           "Ikinyarwanda",         "rw",    nullptr, nullptr},
    {"ko",      QT_TRANSLATE_NOOP("Languages", "Korean"),                "한국어",                "ko",    "KO",    "KO"},
    {"ku",      QT_TRANSLATE_NOOP("Languages", "Kurdish (Kurmanji)"),    "Kurdî",                "ku",    nullptr, nullptr},
    {"ky",      QT_TRANSLATE_NOOP("Languages", "Kyrgyz"),                "Кыргызча",             "ky",    nullptr, nullptr},
    {"lo",      QT_TRANSLATE_NOOP("Languages", "Lao"),                   "ລາວ",                  "lo",    nullptr, nullptr},
    {"la",      QT_TRANSLATE_NOOP("Languages", "Latin"),                 "Latina",               "la",    nullptr, nullptr},
    {"lv",      QT_TRANSLATE_NOOP("Languages", "Latvian"),               "Latviešu",             "lv",    "LV",    "LV"},
    {"lt",      QT_TRANSLATE_NOOP("Languages", "Lithuanian"),            "Lietuvių",             "lt",    "LT",    "LT"},
    {"lb",      QT_TRANSLATE_NOOP("Languages", "Luxembourgish"),         "Lëtzebuergesch",       "lb",    nullptr, nullptr},
    {"mk",      QT_TRANSLATE_NOOP("Languages", "Macedonian"),            "Македонски",           "mk",    nullptr, nullptr},
    {"mg",      QT_TRANSLATE_NOOP("Languages", "Malagasy"),              "Malagasy",             "mg",    nullptr, nullptr},
    {"ms",      QT_TRANSLATE_NOOP("Languages", "Malay"),                 "Bahasa Melayu",        "ms",    nullptr, nullptr},
    {"ml",      QT_TRANSLATE_NOOP("Languages", "Malayalam"),             "മലയാളം",               "ml",    nullptr, nullptr},
    {"mt",      QT_TRANSLATE_NOOP("Languages", "Maltese"),               "Malti",                "mt",    nullptr, nullptr},
    {"mi",      QT_TRANSLATE_NOOP("Languages", "Maori"),                 "Māori",                "mi",    nullptr, nullptr},
    {"mr",      QT_TRANSLATE_NOOP("Languages", "Marathi"),               "मराठी",                 "mr",    nullptr, nullptr},
    {"mn",      QT_TRANSLATE_NOOP("Languages", "Mongolian"),             "Монгол",               "mn",    nullptr, nullptr},
    {"my",      QT_TRANSLATE_NOOP("Languages", "Myanmar (Burmese)"),     "မြန်မာ",                "my",    nullptr, nullptr},
    {"ne",      QT_TRANSLATE_NOOP("Languages", "Nepali"),                "नेपाली",                 "ne",    nullptr, nullptr},
    {"nb",      QT_TRANSLATE_NOOP("Languages", "Norwegian"),             "Norsk bokmål",         "no",    "NB",    "NB"},
    {"or",      QT_TRANSLATE_NOOP("Languages", "Odia"),                  "ଓଡ଼ିଆ",                  "or",    nullptr, nullptr},
    {"ps",      QT_TRANSLATE_NOOP("Languages", "Pashto"),                "پښتو",                 "ps",    nullptr, nullptr},
    {"fa",      QT_TRANSLATE_NOOP("Languages", "Persian"),               "فارسی",                "fa",    nullptr, nullptr},
    {"pl",      QT_TRANSLATE_NOOP("Languages", "Polish"),                "Polski",               "pl",    "PL",    "PL"},
    {"pt-BR",   QT_TRANSLATE_NOOP("Languages", "Portuguese (Brazil)"),   "Português (Brasil)",   "pt",    "PT",    "PT-BR"},
    {"pt-PT",   QT_TRANSLATE_NOOP("Languages", "Portuguese (Portugal)"), "Português (Portugal)", "pt-PT", "PT",    "PT-PT"},
    {"pa",      QT_TRANSLATE_NOOP("Languages", "Punjabi"),               "ਪੰਜਾਬੀ",                "pa",    nullptr, nullptr},
    {"ro",      QT_TRANSLATE_NOOP("Languages", "Romanian"),              "Română",               "ro",    "RO",    "RO"},
    {"ru",      QT_TRANSLATE_NOOP("Languages", "Russian"),               "Русский",              "ru",    "RU",    "RU"},
    {"sm",      QT_TRANSLATE_NOOP("Languages", "Samoan"),                "Gagana Samoa",         "sm",    nullptr, nullptr},
    {"gd",      QT_TRANSLATE_NOOP("Languages", "Scots Gaelic"),          "Gàidhlig",             "gd",    nullptr, nullptr},
    {"sr",      QT_TRANSLATE_NOOP("Languages", "Serbian"),               "Српски",               "sr",    nullptr, nullptr},
    {"st",      QT_TRANSLATE_NOOP("Languages", "Sesotho"),               "Sesotho",              "st",    nullptr, nullptr},
    {"sn",      QT_TRANSLATE_NOOP("Languages", "Shona"),                 "chiShona",             "sn",    nullptr, nullptr},
    {"sd",      QT_TRANSLATE_NOOP("Languages", "Sindhi"),                "سنڌي",                 "sd",    nullptr, nullptr},
    {"si",      QT_TRANSLATE_NOOP("Languages", "Sinhala"),               "සිංහල",                "si",    nullptr, nullptr},
    {"sk",      QT_TRANSLATE_NOOP("Languages", "Slovak"),                "Slovenčina",           "sk",    "SK",    "SK"},
    {"sl",      QT_TRANSLATE_NOOP("Languages", "Slovenian"),             "Slovenščina",          "sl",    "SL",    "SL"},
    {"so",      QT_TRANSLATE_NOOP("Languages", "Somali"),                "Soomaali",             "so",    nullptr, nullptr},
    {"es",      QT_TRANSLATE_NOOP("Languages", "Spanish"),               "Español",              "es",    "ES",    "ES"},
    {"su",      QT_TRANSLATE_NOOP("Languages", "Sundanese"),             "Basa Sunda",           "su",    nullptr, nullptr},
    {"sw",      QT_TRANSLATE_NOOP("Languages", "Swahili"),               "Kiswahili",            "sw",    nullptr, nullptr},
    {"sv",      QT_TRANSLATE_NOOP("Languages", "Swedish"),               "Svenska",              "sv",    "SV",    "SV"},
    {"tg",      QT_TRANSLATE_NOOP("Languages", "Tajik"),                 "Тоҷикӣ",               "tg",    nullptr, nullptr},
    {"ta",      QT_TRANSLATE_NOOP("Languages", "Tamil"),                 "தமிழ்",                 "ta",    nullptr, nullptr},
    {"tt",      QT_TRANSLATE_NOOP("Languages", "Tatar"),                 "Татар",                "tt",    nullptr, nullptr},
    {"te",      QT_TRANSLATE_NOOP("Languages", "Telugu"),                "తెలుగు",                "te",    nullptr, nullptr},
    {"th",      QT_TRANSLATE_NOOP("Languages", "Thai"),                  "ไทย",                  "th",    "TH",    "TH"},
    {"tr",      QT_TRANSLATE_NOOP("Languages", "Turkish"),               "Türkçe",               "tr",    "TR",    "TR"},
    {"tk",      QT_TRANSLATE_NOOP("Languages", "Turkmen"),               "Türkmen",              "tk",    nullptr, nullptr},
    {"uk",      QT_TRANSLATE_NOOP("Languages", "Ukrainian"),             "Українська",           "uk",    "UK",    "UK"},
    {"ur",      QT_TRANSLATE_NOOP("Languages", "Urdu"),                  "اردو",                 "ur",    nullptr, nullptr},
    {"ug",      QT_TRANSLATE_NOOP("Languages", "Uyghur"),                "ئۇيغۇرچە",             "ug",    nullptr, nullptr},
    {"uz",      QT_TRANSLATE_NOOP("Languages", "Uzbek"),                 "Oʻzbek",               "uz",    nullptr, nullptr},
    {"vi",      QT_TRANSLATE_NOOP("Languages", "Vietnamese"),            "Tiếng Việt",           "vi",    "VI",    "VI"},
    {"cy",      QT_TRANSLATE_NOOP("Languages", "Welsh"),                 "Cymraeg",              "cy",    nullptr, nullptr},
    {"xh",      QT_TRANSLATE_NOOP("Languages", "Xhosa"),                 "isiXhosa",             "xh",    nullptr, nullptr},
    {"yi",      QT_TRANSLATE_NOOP("Languages", "Yiddish"),               "ייִדיש",                "yi",    nullptr, nullptr},
    {"yo",      QT_TRANSLATE_NOOP("Languages", "Yoruba"),                "Yorùbá",               "yo",    nullptr, nullptr},
    {"zu",      QT_TRANSLATE_NOOP("Languages", "Zulu"),                  "isiZulu",              "zu",    nullptr, nullptr},
};

// Codes a service may return that differ from the ones it is sent.
struct Synonym
{
    const char *serviceId;
    const char *serviceCode;
    const char *code; // internal code
};

constexpr Synonym kSynonyms[] = {
    {"google", "he",    "he"},      // Google sends back "iw"; both are accepted
    {"google", "jv",    "jv"},      // ... "jw"
    {"google", "zh",    "zh-Hans"},
    {"google", "fil",   "fil"},
    {"google", "nb",    "nb"},
    {"deepl",  "EN-GB", "en"},      // English variant chosen in the DeepL settings
};
// clang-format on

constexpr QLatin1String kGoogle("google");
constexpr QLatin1String kDeepL("deepl");
constexpr QLatin1String kDeepLWeb("deepl-web");
constexpr QLatin1String kLlm("llm");
constexpr QLatin1String kMyMemory("mymemory");
constexpr QLatin1String kMozhi("mozhi");

// MyMemory takes ISO 639-1/RFC 3066 codes and follows the Google catalogue,
// but a few of its codes differ from the internal ones.
struct CodeOverride
{
    const char *code;        // internal code
    const char *serviceCode; // MyMemory code
};

constexpr CodeOverride kMyMemoryOverrides[] = {
    {"zh-Hans", "zh-CN"},
    {"zh-Hant", "zh-TW"},
    {"nb", "no"},
    {"fil", "tl"},
};

const LanguageInfo *findInfo(const QString &code)
{
    for (const LanguageInfo &info : kLanguages) {
        if (code == QLatin1String(info.code))
            return &info;
    }
    return nullptr;
}

const char *mymemoryCodeOf(const LanguageInfo &info)
{
    if (!info.google)
        return nullptr; // the catalogue follows Google's
    for (const CodeOverride &codeOverride : kMyMemoryOverrides) {
        if (QLatin1String(info.code) == QLatin1String(codeOverride.code))
            return codeOverride.serviceCode;
    }
    return info.code;
}

// Mozhi expects Google-style codes and converts them per engine, so the
// Google column is reused as is; this function is the single fix point if a
// later engine needs a different code.
const char *mozhiCodeOf(const LanguageInfo &info)
{
    return info.google;
}

const char *serviceCodeOf(const LanguageInfo &info, const QString &serviceId,
                          Languages::Direction d)
{
    if (serviceId == kGoogle)
        return info.google;
    if (serviceId == kDeepL || serviceId == kDeepLWeb)
        return d == Languages::Direction::Source ? info.deeplSource : info.deeplTarget;
    // LLMs accept the internal codes; the catalogue is the same as Google's.
    if (serviceId == kLlm)
        return info.google ? info.code : nullptr;
    if (serviceId == kMyMemory)
        return mymemoryCodeOf(info);
    if (serviceId == kMozhi)
        return mozhiCodeOf(info);
    return nullptr;
}

bool sameCode(const char *a, const QString &b)
{
    return a && b.compare(QLatin1String(a), Qt::CaseInsensitive) == 0;
}

} // namespace

namespace Languages {

QString autoCode()
{
    return QStringLiteral("auto");
}

QStringList allCodes()
{
    QStringList codes;
    codes.reserve(int(std::size(kLanguages)));
    for (const LanguageInfo &info : kLanguages)
        codes << QLatin1String(info.code);
    return codes;
}

bool isKnown(const QString &code)
{
    return findInfo(code) != nullptr;
}

QString displayName(const QString &code)
{
    if (code == autoCode())
        return QCoreApplication::translate("Languages", "Auto-detect");
    if (const LanguageInfo *info = findInfo(code))
        return QCoreApplication::translate("Languages", info->englishName);
    return code;
}

QString englishName(const QString &code)
{
    const LanguageInfo *info = findInfo(code);
    return info ? QString::fromUtf8(info->englishName) : code;
}

QString nativeName(const QString &code)
{
    const LanguageInfo *info = findInfo(code);
    return info ? QString::fromUtf8(info->nativeName) : QString();
}

QString toServiceCode(const QString &serviceId, const QString &code, Direction direction)
{
    const LanguageInfo *info = findInfo(code);
    if (!info)
        return {};
    const char *serviceCode = serviceCodeOf(*info, serviceId, direction);
    return serviceCode ? QString::fromLatin1(serviceCode) : QString();
}

QString fromServiceCode(const QString &serviceId, const QString &serviceCode, Direction direction)
{
    if (serviceCode.isEmpty())
        return {};

    // The code for the requested direction first, then the other direction
    // (DeepL "EN-US" reported as a source, "PT" as a target), then synonyms.
    const Direction other = direction == Direction::Source ? Direction::Target : Direction::Source;
    for (Direction d : {direction, other}) {
        for (const LanguageInfo &info : kLanguages) {
            if (sameCode(serviceCodeOf(info, serviceId, d), serviceCode))
                return QLatin1String(info.code);
        }
    }
    for (const Synonym &synonym : kSynonyms) {
        if (serviceId == QLatin1String(synonym.serviceId)
            && sameCode(synonym.serviceCode, serviceCode)) {
            return QLatin1String(synonym.code);
        }
    }
    return {};
}

QStringList supported(const QString &serviceId, Direction direction)
{
    QStringList codes;
    for (const LanguageInfo &info : kLanguages) {
        if (serviceCodeOf(info, serviceId, direction))
            codes << QLatin1String(info.code);
    }
    return codes;
}

QString closest(const QString &serviceId, const QString &code, Direction direction)
{
    if (code == autoCode())
        return code;

    const QStringList codes = supported(serviceId, direction);
    if (codes.isEmpty())
        return {};
    if (codes.contains(code))
        return code;

    const QString base = code.section(QLatin1Char('-'), 0, 0);
    for (const QString &candidate : codes) {
        if (candidate.section(QLatin1Char('-'), 0, 0) == base)
            return candidate;
    }

    const QString system = systemLanguage();
    if (codes.contains(system))
        return system;
    if (codes.contains(QStringLiteral("en")))
        return QStringLiteral("en");
    return codes.first();
}

QString forLocale(const QLocale &locale)
{
    switch (locale.language()) {
    case QLocale::Chinese: {
        const QLocale::Territory t = locale.territory();
        const bool traditional = locale.script() == QLocale::TraditionalHanScript
                                 || t == QLocale::Taiwan || t == QLocale::HongKong
                                 || t == QLocale::Macao;
        return traditional ? QStringLiteral("zh-Hant") : QStringLiteral("zh-Hans");
    }
    case QLocale::Portuguese:
        return locale.territory() == QLocale::Brazil ? QStringLiteral("pt-BR")
                                                     : QStringLiteral("pt-PT");
    case QLocale::NorwegianBokmal:
    case QLocale::NorwegianNynorsk:
        return QStringLiteral("nb");
    default:
        break;
    }

    const QString code = QLocale::languageToCode(locale.language());
    return isKnown(code) ? code : QStringLiteral("en");
}

QString systemLanguage()
{
    return forLocale(QLocale::system());
}

} // namespace Languages

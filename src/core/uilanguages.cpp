#include "core/uilanguages.h"

#include "core/languages.h"

#include <QLatin1Char>

namespace UiLanguages {
namespace {

const QVector<Info> &languages()
{
    static const QVector<Info> table = {
        {QStringLiteral("en"), QStringLiteral("English"), QString()},
        {QStringLiteral("ru"), QStringLiteral("Русский"),
         QStringLiteral(":/i18n/qtranslate_ru_RU.qm")},
        {QStringLiteral("es"), QStringLiteral("Español"),
         QStringLiteral(":/i18n/qtranslate_es.qm")},
        {QStringLiteral("de"), QStringLiteral("Deutsch"),
         QStringLiteral(":/i18n/qtranslate_de.qm")},
        {QStringLiteral("fr"), QStringLiteral("Français"),
         QStringLiteral(":/i18n/qtranslate_fr.qm")},
        {QStringLiteral("pt-BR"), QStringLiteral("Português (Brasil)"),
         QStringLiteral(":/i18n/qtranslate_pt_BR.qm")},
        {QStringLiteral("it"), QStringLiteral("Italiano"),
         QStringLiteral(":/i18n/qtranslate_it.qm")},
        {QStringLiteral("pl"), QStringLiteral("Polski"),
         QStringLiteral(":/i18n/qtranslate_pl.qm")},
        {QStringLiteral("uk"), QStringLiteral("Українська"),
         QStringLiteral(":/i18n/qtranslate_uk.qm")},
        {QStringLiteral("tr"), QStringLiteral("Türkçe"),
         QStringLiteral(":/i18n/qtranslate_tr.qm")},
        {QStringLiteral("zh-Hans"), QStringLiteral("简体中文"),
         QStringLiteral(":/i18n/qtranslate_zh_CN.qm")},
        {QStringLiteral("ja"), QStringLiteral("日本語"),
         QStringLiteral(":/i18n/qtranslate_ja.qm")},
        {QStringLiteral("ko"), QStringLiteral("한국어"),
         QStringLiteral(":/i18n/qtranslate_ko.qm")},
        {QStringLiteral("ar"), QStringLiteral("العربية"),
         QStringLiteral(":/i18n/qtranslate_ar.qm")},
        {QStringLiteral("hi"), QStringLiteral("हिन्दी"),
         QStringLiteral(":/i18n/qtranslate_hi.qm")},
    };
    return table;
}

} // namespace

const QVector<Info> &all()
{
    return languages();
}

QString normalize(const QString &code)
{
    QString value = code.trimmed();
    if (value.isEmpty() || value == QStringLiteral("system"))
        value = Languages::systemLanguage();

    // Traditional Chinese is not translated yet; do not show Simplified
    // Chinese to users who explicitly selected a Traditional locale.
    if (value.startsWith(QStringLiteral("zh-Hant"), Qt::CaseInsensitive)
        || value.startsWith(QStringLiteral("zh-TW"), Qt::CaseInsensitive)
        || value.startsWith(QStringLiteral("zh-HK"), Qt::CaseInsensitive)) {
        return QStringLiteral("en");
    }
    if (value.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive))
        return QStringLiteral("zh-Hans");
    if (value.startsWith(QStringLiteral("pt"), Qt::CaseInsensitive))
        return QStringLiteral("pt-BR");

    const QString base =
        value.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0);
    for (const Info &language : languages()) {
        if (language.code.compare(value, Qt::CaseInsensitive) == 0
            || language.code.compare(base, Qt::CaseInsensitive) == 0) {
            return language.code;
        }
    }
    return QStringLiteral("en");
}

QString resourceFor(const QString &code)
{
    for (const Info &language : languages()) {
        if (language.code == code)
            return language.resource;
    }
    return {};
}

QString nativeName(const QString &code)
{
    for (const Info &language : languages()) {
        if (language.code == code)
            return language.nativeName;
    }
    return code;
}

bool isSupported(const QString &code)
{
    if (code == QStringLiteral("system"))
        return true;
    for (const Info &language : languages()) {
        if (language.code == code)
            return true;
    }
    return false;
}

QString systemDefault()
{
    return normalize(Languages::systemLanguage());
}

} // namespace UiLanguages

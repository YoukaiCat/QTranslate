#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>

// Language catalogue and mapping between internal language codes ("en", "ru",
// "pt-BR", "zh-Hans", ...) and the codes of each translation service
// (see doc/architecture.md, §4.8).
namespace Languages {

enum class Direction { Source, Target };

// Pseudo-code for automatic source language detection.
QString autoCode();

// All known internal codes, in catalogue order.
QStringList allCodes();

bool isKnown(const QString &code);

// Name for the UI, translatable (English by default). "Auto-detect" for
// autoCode(); the code itself for an unknown code.
QString displayName(const QString &code);

// Name of the language in that language, e.g. "Deutsch". Empty if unknown.
QString nativeName(const QString &code);

// Internal code -> service code, e.g. ("google", "zh-Hans") -> "zh-CN".
// Empty if the service does not support the language in this direction.
QString toServiceCode(const QString &serviceId, const QString &code, Direction direction);

// Service code -> internal code, case-insensitive, accepting known synonyms
// (Google "iw" for Hebrew, DeepL "EN-GB"). Empty if unknown.
QString fromServiceCode(const QString &serviceId, const QString &serviceCode, Direction direction);

// Internal codes supported by the service in this direction, without autoCode().
QStringList supported(const QString &serviceId, Direction direction);

// Best supported code for `code` after a service switch: exact code, a code
// with the same base language (pt-BR/pt-PT, zh-Hans/zh-Hant), the system
// language, "en", then the first supported language. Returns an empty string
// only if the service has no languages in this direction.
QString closest(const QString &serviceId, const QString &code, Direction direction);

// Best internal code for a locale, taking script and region into account
// (zh-Hans/zh-Hant, pt-BR/pt-PT). Falls back to "en".
QString forLocale(const QLocale &locale);

// forLocale(QLocale::system()).
QString systemLanguage();

} // namespace Languages

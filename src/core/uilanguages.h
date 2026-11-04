#pragma once

#include <QString>
#include <QVector>

// Languages the application interface can be translated into. The codes match
// the internal language codes used by Languages::systemLanguage(); "system"
// is resolved by normalize().
namespace UiLanguages {

struct Info
{
    QString code;
    QString nativeName;
    QString resource;
};

const QVector<Info> &all();

// Resolves "system", language tags such as de-DE and pt-PT, and unknown codes
// to a supported UI language. Unsupported languages fall back to English.
QString normalize(const QString &code);

// Returns the supported canonical code for an alias such as de-DE or pt-PT,
// or an empty string when the code is unknown or explicitly unsupported.
QString canonical(const QString &code);

// Empty for English, which is the source language and needs no translator.
QString resourceFor(const QString &code);

// Qt's own translation file for standard dialogs (qt_<locale>.qm), or empty
// when Qt does not provide one (English; Qt 6.12 has no Hindi translation).
QString qtTranslationFile(const QString &code);

QString nativeName(const QString &code);

bool isSupported(const QString &code);

// Supported language selected for the current system locale.
QString systemDefault();

} // namespace UiLanguages

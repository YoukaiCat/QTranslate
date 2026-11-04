#pragma once

#include <QComboBox>
#include <QStringList>

// Language selector working with internal language codes (core/languages).
class LanguageComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit LanguageComboBox(QWidget *parent = nullptr);

    // Fills the list with `codes` sorted by name, optionally with "Auto-detect"
    // on top. Keeps the current language if it is still in the list.
    void setLanguages(const QStringList &codes, bool includeAuto);

    // Empty when the list is empty.
    QString currentCode() const;
    // Returns false (and changes nothing) if the code is not in the list.
    bool setCurrentCode(const QString &code);

signals:
    void currentCodeChanged(const QString &code);
};

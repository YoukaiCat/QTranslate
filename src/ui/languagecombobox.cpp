#include "ui/languagecombobox.h"

#include "core/languages.h"

#include <QSignalBlocker>

#include <algorithm>

LanguageComboBox::LanguageComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setMinimumContentsLength(14);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMaxVisibleItems(20);
    connect(this, &QComboBox::currentIndexChanged, this,
            [this] { emit currentCodeChanged(currentCode()); });
}

void LanguageComboBox::setLanguages(const QStringList &codes, bool includeAuto)
{
    const QString previous = currentCode();

    struct Item
    {
        QString name;
        QString code;
    };
    QList<Item> items;
    items.reserve(codes.size());
    for (const QString &code : codes)
        items.append({Languages::displayName(code), code});
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });

    {
        const QSignalBlocker blocker(this);
        clear();
        if (includeAuto)
            addItem(Languages::displayName(Languages::autoCode()), Languages::autoCode());
        for (const Item &item : items) {
            addItem(item.name, item.code);
            setItemData(count() - 1, Languages::nativeName(item.code), Qt::ToolTipRole);
        }
        const int index = findData(previous);
        setCurrentIndex(index >= 0 ? index : 0);
    }
    if (currentCode() != previous)
        emit currentCodeChanged(currentCode());
}

QString LanguageComboBox::currentCode() const
{
    return currentData().toString();
}

bool LanguageComboBox::setCurrentCode(const QString &code)
{
    const int index = findData(code);
    if (index < 0)
        return false;
    setCurrentIndex(index);
    return true;
}

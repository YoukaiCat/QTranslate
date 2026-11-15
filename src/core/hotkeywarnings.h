#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Global hotkeys whose last registration attempt failed. The key is
// int(HotkeyAction): HotkeyAction belongs to the platform layer, so core
// keeps it as a number. There is no "registered" signal; an entry is dropped
// before the next attempt for its action and re-added if that attempt fails.
class HotkeyWarnings
{
public:
    void clear();                                       // a fresh start pass
    void forget(int action);                            // before a new attempt
    void record(int action, const QString &sequence);   // an empty text is ignored
    bool isEmpty() const;
    QList<int> actions() const;                         // ascending, i.e. HotkeyAction order
    QStringList sequences() const;                      // same order, duplicates dropped

private:
    QMap<int, QString> m_sequences;
};

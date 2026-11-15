#include "core/hotkeywarnings.h"

void HotkeyWarnings::clear()
{
    m_sequences.clear();
}

void HotkeyWarnings::forget(int action)
{
    m_sequences.remove(action);
}

void HotkeyWarnings::record(int action, const QString &sequence)
{
    if (sequence.isEmpty())
        return;
    m_sequences.insert(action, sequence);
}

bool HotkeyWarnings::isEmpty() const
{
    return m_sequences.isEmpty();
}

QList<int> HotkeyWarnings::actions() const
{
    return m_sequences.keys(); // QMap keeps its keys in ascending order
}

QStringList HotkeyWarnings::sequences() const
{
    QStringList result;
    for (const QString &sequence : m_sequences) {
        if (!result.contains(sequence))
            result << sequence;
    }
    return result;
}

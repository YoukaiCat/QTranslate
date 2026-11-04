#pragma once

#include <QKeySequence>
#include <QtGlobal>

// Conversion between a QKeySequence and the native Windows hotkey form
// (MOD_* + VK_*) used by RegisterHotKey (see doc/architecture.md, §4.3).
// Pure functions without WinAPI so that they can be unit-tested in
// qtranslate_core.
namespace HotkeySequence {

// Win32 MOD_* values.
enum Modifier : quint32 {
    ModAlt = 0x0001,
    ModControl = 0x0002,
    ModShift = 0x0004,
    ModWin = 0x0008,
};

struct Hotkey
{
    quint32 modifiers = 0;
    quint32 virtualKey = 0; // 0 means "not a valid hotkey"

    bool isValid() const { return virtualKey != 0; }
    bool operator==(const Hotkey &other) const
    {
        return modifiers == other.modifiers && virtualKey == other.virtualKey;
    }
    bool operator!=(const Hotkey &other) const { return !(*this == other); }
};

// Returns an invalid Hotkey for an empty sequence, a modifier-only sequence,
// a sequence with more than one chord, or an unsupported key.
Hotkey fromKeySequence(const QKeySequence &sequence);

// Returns an empty QKeySequence for an invalid Hotkey.
QKeySequence toKeySequence(const Hotkey &hotkey);

} // namespace HotkeySequence

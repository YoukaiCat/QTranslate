#include "core/hotkeysequence.h"

namespace {

// Win32 virtual-key codes (windows.h is intentionally not included: this file
// belongs to qtranslate_core and must stay platform-independent).
enum : quint32 {
    VkBack = 0x08,
    VkTab = 0x09,
    VkClear = 0x0C,
    VkReturn = 0x0D,
    VkPause = 0x13,
    VkCapital = 0x14,
    VkEscape = 0x1B,
    VkSpace = 0x20,
    VkPageUp = 0x21,
    VkPageDown = 0x22,
    VkEnd = 0x23,
    VkHome = 0x24,
    VkLeft = 0x25,
    VkUp = 0x26,
    VkRight = 0x27,
    VkDown = 0x28,
    VkPrint = 0x2C,
    VkInsert = 0x2D,
    VkDelete = 0x2E,
    VkApps = 0x5D,
    VkNumPad0 = 0x60,
    VkMultiply = 0x6A,
    VkAdd = 0x6B,
    VkSubtract = 0x6D,
    VkDecimal = 0x6E,
    VkDivide = 0x6F,
    VkF1 = 0x70,
    VkNumLock = 0x90,
    VkScroll = 0x91,
    VkOem1 = 0xBA,      // ;:
    VkOemPlus = 0xBB,   // =+
    VkOemComma = 0xBC,  // ,<
    VkOemMinus = 0xBD,  // -_
    VkOemPeriod = 0xBE, // .>
    VkOem2 = 0xBF,      // /?
    VkOem3 = 0xC0,      // `~
    VkOem4 = 0xDB,      // [{
    VkOem5 = 0xDC,      // \|
    VkOem6 = 0xDD,      // ]}
    VkOem7 = 0xDE,      // '"
};

quint32 modifierBits(Qt::KeyboardModifiers modifiers)
{
    quint32 bits = 0;
    if (modifiers & Qt::AltModifier)
        bits |= HotkeySequence::ModAlt;
    if (modifiers & Qt::ControlModifier)
        bits |= HotkeySequence::ModControl;
    // On Windows AltGr is reported as Ctrl+Alt (or as GroupSwitch by Qt).
    // RegisterHotKey only knows MOD_CONTROL | MOD_ALT, so normalize it.
    if (modifiers & Qt::GroupSwitchModifier)
        bits |= HotkeySequence::ModControl | HotkeySequence::ModAlt;
    if (modifiers & Qt::ShiftModifier)
        bits |= HotkeySequence::ModShift;
    if (modifiers & Qt::MetaModifier)
        bits |= HotkeySequence::ModWin;
    return bits;
}

Qt::KeyboardModifiers qtModifiers(quint32 bits)
{
    Qt::KeyboardModifiers modifiers;
    if (bits & HotkeySequence::ModAlt)
        modifiers |= Qt::AltModifier;
    if (bits & HotkeySequence::ModControl)
        modifiers |= Qt::ControlModifier;
    if (bits & HotkeySequence::ModShift)
        modifiers |= Qt::ShiftModifier;
    if (bits & HotkeySequence::ModWin)
        modifiers |= Qt::MetaModifier;
    return modifiers;
}

// `implicitShift` reports keys that are typed with Shift on a US layout, e.g.
// Key_Exclam is Shift+1. RegisterHotKey needs the physical key plus Shift.
quint32 virtualKeyForQtKey(int key, bool keypad, bool *implicitShift)
{
    *implicitShift = false;

    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return quint32('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        if (keypad)
            return VkNumPad0 + quint32(key - Qt::Key_0);
        return quint32('0' + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VkF1 + quint32(key - Qt::Key_F1);

    switch (key) {
    case Qt::Key_Escape:
        return VkEscape;
    case Qt::Key_Tab:
        return VkTab;
    case Qt::Key_Backspace:
        return VkBack;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VkReturn;
    case Qt::Key_Insert:
        return VkInsert;
    case Qt::Key_Delete:
        return VkDelete;
    case Qt::Key_Home:
        return VkHome;
    case Qt::Key_End:
        return VkEnd;
    case Qt::Key_PageUp:
        return VkPageUp;
    case Qt::Key_PageDown:
        return VkPageDown;
    case Qt::Key_Left:
        return VkLeft;
    case Qt::Key_Up:
        return VkUp;
    case Qt::Key_Right:
        return VkRight;
    case Qt::Key_Down:
        return VkDown;
    case Qt::Key_Pause:
        return VkPause;
    case Qt::Key_Print:
        return VkPrint;
    case Qt::Key_Clear:
        return VkClear;
    case Qt::Key_CapsLock:
        return VkCapital;
    case Qt::Key_NumLock:
        return VkNumLock;
    case Qt::Key_ScrollLock:
        return VkScroll;
    case Qt::Key_Menu:
        return VkApps;
    case Qt::Key_Space:
        return VkSpace;

    // Punctuation (US layout OEM keys).
    case Qt::Key_Semicolon:
        return VkOem1;
    case Qt::Key_Equal:
        return VkOemPlus;
    case Qt::Key_Comma:
        return VkOemComma;
    case Qt::Key_Minus:
        return VkOemMinus;
    case Qt::Key_Period:
        return VkOemPeriod;
    case Qt::Key_Slash:
        return keypad ? VkDivide : VkOem2;
    case Qt::Key_QuoteLeft:
        return VkOem3;
    case Qt::Key_BracketLeft:
        return VkOem4;
    case Qt::Key_Backslash:
        return VkOem5;
    case Qt::Key_BracketRight:
        return VkOem6;
    case Qt::Key_Apostrophe:
        return VkOem7;

    // Shifted punctuation: the base key plus Shift.
    case Qt::Key_Exclam:
        *implicitShift = true;
        return '1';
    case Qt::Key_At:
        *implicitShift = true;
        return '2';
    case Qt::Key_NumberSign:
        *implicitShift = true;
        return '3';
    case Qt::Key_Dollar:
        *implicitShift = true;
        return '4';
    case Qt::Key_Percent:
        *implicitShift = true;
        return '5';
    case Qt::Key_AsciiCircum:
        *implicitShift = true;
        return '6';
    case Qt::Key_Ampersand:
        *implicitShift = true;
        return '7';
    case Qt::Key_Asterisk:
        if (keypad)
            return VkMultiply;
        *implicitShift = true;
        return '8';
    case Qt::Key_ParenLeft:
        *implicitShift = true;
        return '9';
    case Qt::Key_ParenRight:
        *implicitShift = true;
        return '0';
    case Qt::Key_Underscore:
        *implicitShift = true;
        return VkOemMinus;
    case Qt::Key_Plus:
        if (keypad)
            return VkAdd;
        *implicitShift = true;
        return VkOemPlus;
    case Qt::Key_BraceLeft:
        *implicitShift = true;
        return VkOem4;
    case Qt::Key_BraceRight:
        *implicitShift = true;
        return VkOem6;
    case Qt::Key_Bar:
        *implicitShift = true;
        return VkOem5;
    case Qt::Key_Colon:
        *implicitShift = true;
        return VkOem1;
    case Qt::Key_QuoteDbl:
        *implicitShift = true;
        return VkOem7;
    case Qt::Key_Less:
        *implicitShift = true;
        return VkOemComma;
    case Qt::Key_Greater:
        *implicitShift = true;
        return VkOemPeriod;
    case Qt::Key_Question:
        *implicitShift = true;
        return VkOem2;
    case Qt::Key_AsciiTilde:
        *implicitShift = true;
        return VkOem3;

    default:
        break;
    }

    return 0;
}

int qtKeyForVirtualKey(quint32 vk, bool *keypad)
{
    *keypad = false;

    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return int(vk);
    if (vk >= VkF1 && vk <= VkF1 + 23)
        return Qt::Key_F1 + int(vk - VkF1);
    if (vk >= VkNumPad0 && vk <= VkNumPad0 + 9) {
        *keypad = true;
        return Qt::Key_0 + int(vk - VkNumPad0);
    }

    switch (vk) {
    case VkEscape:
        return Qt::Key_Escape;
    case VkTab:
        return Qt::Key_Tab;
    case VkBack:
        return Qt::Key_Backspace;
    case VkReturn:
        return Qt::Key_Return;
    case VkInsert:
        return Qt::Key_Insert;
    case VkDelete:
        return Qt::Key_Delete;
    case VkHome:
        return Qt::Key_Home;
    case VkEnd:
        return Qt::Key_End;
    case VkPageUp:
        return Qt::Key_PageUp;
    case VkPageDown:
        return Qt::Key_PageDown;
    case VkLeft:
        return Qt::Key_Left;
    case VkUp:
        return Qt::Key_Up;
    case VkRight:
        return Qt::Key_Right;
    case VkDown:
        return Qt::Key_Down;
    case VkPause:
        return Qt::Key_Pause;
    case VkPrint:
        return Qt::Key_Print;
    case VkClear:
        return Qt::Key_Clear;
    case VkCapital:
        return Qt::Key_CapsLock;
    case VkNumLock:
        return Qt::Key_NumLock;
    case VkScroll:
        return Qt::Key_ScrollLock;
    case VkApps:
        return Qt::Key_Menu;
    case VkSpace:
        return Qt::Key_Space;
    case VkOem1:
        return Qt::Key_Semicolon;
    case VkOemPlus:
        return Qt::Key_Equal;
    case VkOemComma:
        return Qt::Key_Comma;
    case VkOemMinus:
        return Qt::Key_Minus;
    case VkOemPeriod:
        return Qt::Key_Period;
    case VkOem2:
        return Qt::Key_Slash;
    case VkOem3:
        return Qt::Key_QuoteLeft;
    case VkOem4:
        return Qt::Key_BracketLeft;
    case VkOem5:
        return Qt::Key_Backslash;
    case VkOem6:
        return Qt::Key_BracketRight;
    case VkOem7:
        return Qt::Key_Apostrophe;
    case VkMultiply:
        *keypad = true;
        return Qt::Key_Asterisk;
    case VkAdd:
        *keypad = true;
        return Qt::Key_Plus;
    case VkSubtract:
        *keypad = true;
        return Qt::Key_Minus;
    case VkDecimal:
        *keypad = true;
        return Qt::Key_Period;
    case VkDivide:
        *keypad = true;
        return Qt::Key_Slash;
    default:
        break;
    }

    return 0;
}

} // namespace

namespace HotkeySequence {

Hotkey fromKeySequence(const QKeySequence &sequence)
{
    if (sequence.isEmpty() || sequence.count() != 1)
        return {};

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const Qt::Key key = combination.key();

    const bool keypad = modifiers & Qt::KeypadModifier;
    bool implicitShift = false;
    const quint32 virtualKey = virtualKeyForQtKey(int(key), keypad, &implicitShift);
    if (virtualKey == 0)
        return {};

    quint32 bits = modifierBits(modifiers);
    if (implicitShift)
        bits |= ModShift;
    return Hotkey{bits, virtualKey};
}

QKeySequence toKeySequence(const Hotkey &hotkey)
{
    if (!hotkey.isValid() || (hotkey.modifiers & ~(ModAlt | ModControl | ModShift | ModWin)))
        return {};

    bool keypad = false;
    const int key = qtKeyForVirtualKey(hotkey.virtualKey, &keypad);
    if (key == 0)
        return {};

    Qt::KeyboardModifiers modifiers = qtModifiers(hotkey.modifiers);
    if (keypad)
        modifiers |= Qt::KeypadModifier;
    return QKeySequence(QKeyCombination(modifiers, Qt::Key(key)));
}

} // namespace HotkeySequence

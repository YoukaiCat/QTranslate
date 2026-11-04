#include "core/hotkeysequence.h"

#include <QDebug>
#include <QTest>

using HotkeySequence::Hotkey;
using HotkeySequence::ModAlt;
using HotkeySequence::ModControl;
using HotkeySequence::ModShift;
using HotkeySequence::ModWin;

namespace {

constexpr quint32 VkBack = 0x08;
constexpr quint32 VkTab = 0x09;
constexpr quint32 VkReturn = 0x0D;
constexpr quint32 VkPause = 0x13;
constexpr quint32 VkEscape = 0x1B;
constexpr quint32 VkSpace = 0x20;
constexpr quint32 VkEnd = 0x23;
constexpr quint32 VkHome = 0x24;
constexpr quint32 VkDown = 0x28;
constexpr quint32 VkPrint = 0x2C;
constexpr quint32 VkInsert = 0x2D;
constexpr quint32 VkDelete = 0x2E;
constexpr quint32 VkNumPad1 = 0x61;
constexpr quint32 VkF1 = 0x70;
constexpr quint32 VkF4 = 0x73;
constexpr quint32 VkF12 = 0x7B;
constexpr quint32 VkF24 = 0x87;
constexpr quint32 VkOem1 = 0xBA;
constexpr quint32 VkOemPlus = 0xBB;
constexpr quint32 VkOem2 = 0xBF;
constexpr quint32 VkOem3 = 0xC0;
constexpr quint32 VkAdd = 0x6B;
constexpr quint32 VkOem7 = 0xDE;

} // namespace

namespace HotkeySequence {

QDebug operator<<(QDebug debug, const Hotkey &hotkey)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "Hotkey(" << hotkey.modifiers << ", " << hotkey.virtualKey << ')';
    return debug;
}

} // namespace HotkeySequence

class TestHotkeySequence : public QObject
{
    Q_OBJECT

private slots:
    void emptySequence()
    {
        QVERIFY(!HotkeySequence::fromKeySequence(QKeySequence()).isValid());
        QVERIFY(HotkeySequence::toKeySequence(Hotkey{}).isEmpty());
    }

    void lettersAndDigits()
    {
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_W)),
                 (Hotkey{ModControl, 'W'}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::ALT | Qt::Key_F4)),
                 (Hotkey{ModAlt, VkF4}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_1)),
                 (Hotkey{ModControl, '1'}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F12)),
                 (Hotkey{ModControl | ModShift, VkF12}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::META | Qt::Key_Space)),
                 (Hotkey{ModWin, VkSpace}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Delete)),
                 (Hotkey{ModControl | ModAlt, VkDelete}));
    }

    void functionKeys()
    {
        for (int i = 1; i <= 24; ++i) {
            const int key = Qt::Key_F1 + (i - 1);
            QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(key)),
                     (Hotkey{0, VkF1 + quint32(i - 1)}));
        }
    }

    void navigationKeys()
    {
        const struct {
            int key;
            quint32 virtualKey;
        } keys[] = {
            {Qt::Key_Escape, VkEscape},   {Qt::Key_Tab, VkTab},
            {Qt::Key_Backspace, VkBack},  {Qt::Key_Return, VkReturn},
            {Qt::Key_Insert, VkInsert},   {Qt::Key_Home, VkHome},
            {Qt::Key_End, VkEnd},         {Qt::Key_Down, VkDown},
            {Qt::Key_Pause, VkPause},     {Qt::Key_Print, VkPrint},
        };
        for (const auto &entry : keys) {
            QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(entry.key)),
                     (Hotkey{0, entry.virtualKey}));
        }
    }

    void punctuation()
    {
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_Semicolon)),
                 (Hotkey{ModControl, VkOem1}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_Equal)),
                 (Hotkey{ModControl, VkOemPlus}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_Slash)),
                 (Hotkey{ModControl, VkOem2}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft)),
                 (Hotkey{ModControl, VkOem3}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe)),
                 (Hotkey{ModControl, VkOem7}));
    }

    void shiftedPunctuationImpliesShift()
    {
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_Exclam)),
                 (Hotkey{ModControl | ModShift, '1'}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::Key_Plus)),
                 (Hotkey{ModControl | ModShift, VkOemPlus}));
        // An explicit Shift does not duplicate.
        QCOMPARE(HotkeySequence::fromKeySequence(
                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Exclam)),
                 (Hotkey{ModControl | ModShift, '1'}));
    }

    void numpad()
    {
        const QKeySequence sequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_1);
        QCOMPARE(HotkeySequence::fromKeySequence(sequence), (Hotkey{ModControl, VkNumPad1}));
        QCOMPARE(HotkeySequence::fromKeySequence(QKeySequence(Qt::KeypadModifier | Qt::Key_Plus)),
                 (Hotkey{0, VkAdd}));
    }

    void unsupportedSequences()
    {
        // Modifier only.
        QVERIFY(!HotkeySequence::fromKeySequence(QKeySequence(int(Qt::CTRL))).isValid());
        QVERIFY(!HotkeySequence::fromKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT)).isValid());
        // More than one chord.
        QVERIFY(!HotkeySequence::fromKeySequence(QKeySequence(QStringLiteral("Ctrl+K, Ctrl+C")))
                     .isValid());
        // Keys with no Windows virtual-key equivalent.
        QVERIFY(!HotkeySequence::fromKeySequence(QKeySequence(Qt::Key_MediaPlay)).isValid());
        QVERIFY(!HotkeySequence::fromKeySequence(QKeySequence(Qt::Key_LaunchMail)).isValid());
    }

    void roundTrip()
    {
        const QKeySequence sequences[] = {
            QKeySequence(Qt::CTRL | Qt::Key_W),
            QKeySequence(Qt::ALT | Qt::Key_F4),
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5),
            QKeySequence(Qt::CTRL | Qt::Key_Semicolon),
            QKeySequence(Qt::ALT | Qt::Key_Insert),
            QKeySequence(Qt::META | Qt::Key_Down),
            QKeySequence(Qt::CTRL | Qt::Key_Return),
        };
        for (const QKeySequence &sequence : sequences) {
            const Hotkey hotkey = HotkeySequence::fromKeySequence(sequence);
            QVERIFY(hotkey.isValid());
            QCOMPARE(HotkeySequence::fromKeySequence(HotkeySequence::toKeySequence(hotkey)),
                     hotkey);
        }
    }

    void invalidHotkey()
    {
        QVERIFY(HotkeySequence::toKeySequence(Hotkey{ModControl, 0}).isEmpty());
        QVERIFY(HotkeySequence::toKeySequence(Hotkey{0x40, 'A'}).isEmpty());
        QVERIFY(HotkeySequence::toKeySequence(Hotkey{ModControl, 0xFF}).isEmpty());
    }
};

QTEST_MAIN(TestHotkeySequence)
#include "tst_hotkeysequence.moc"

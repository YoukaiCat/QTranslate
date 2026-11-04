#pragma once

#include "core/hotkeysequence.h"

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

enum class HotkeyAction {
    TranslateToPopup,
    TranslateToMainWindow,
    ShowMainWindow,
};

Q_DECLARE_METATYPE(HotkeyAction)

// Global hotkeys through RegisterHotKey (see doc/architecture.md, §4.3).
// The header has no WinAPI types; the implementation is hotkeymanager_win.cpp.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // Returns false if the sequence is unsupported or already owned by another
    // program. An existing registration of the same action is replaced.
    bool registerHotkey(HotkeyAction action, const QKeySequence &sequence);
    void unregisterHotkey(HotkeyAction action);
    void unregisterAll();

    // While suspended, nothing is registered; the sequences are remembered and
    // registered again when the suspension ends.
    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }

    QKeySequence hotkey(HotkeyAction action) const;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void activated(HotkeyAction action);
    void registrationFailed(HotkeyAction action, const QKeySequence &sequence,
                            const QString &reason);

private:
    struct Registration
    {
        QKeySequence sequence;
        HotkeySequence::Hotkey hotkey;
        bool registered = false;
    };

    static int idForAction(HotkeyAction action);
    bool registerNative(HotkeyAction action, Registration &registration);

    QHash<int, Registration> m_registrations; // keyed by int(action)
    QHash<int, HotkeyAction> m_actionsById;
    bool m_suspended = false;
};

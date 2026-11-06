#pragma once

#include "core/hotkeysequence.h"

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

#if defined(Q_OS_LINUX)
#include <memory>
#endif

enum class HotkeyAction {
    TranslateToPopup,
    TranslateAndReplace,
    TranslateToMainWindow,
    ShowMainWindow,
};

Q_DECLARE_METATYPE(HotkeyAction)

// Global hotkeys (see doc/architecture.md, §4.3).
// The header has no native types; the implementation is hotkeymanager_win.cpp on
// Windows and hotkeymanager_x11.cpp on Linux.
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
#if defined(Q_OS_WIN)
        HotkeySequence::Hotkey hotkey;
#elif defined(Q_OS_LINUX)
        quint32 keycode = 0;
        quint32 modifiers = 0;
#endif
        bool registered = false;
    };

#if defined(Q_OS_WIN)
    static int idForAction(HotkeyAction action);
#elif defined(Q_OS_LINUX)
    struct X11State;
#endif
    bool registerNative(HotkeyAction action, Registration &registration);

    QHash<int, Registration> m_registrations; // keyed by int(action)
#if defined(Q_OS_WIN)
    QHash<int, HotkeyAction> m_actionsById;
#elif defined(Q_OS_LINUX)
    std::unique_ptr<X11State> m_x11;
#endif
    bool m_suspended = false;
};

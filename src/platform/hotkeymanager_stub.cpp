#include "platform/hotkeymanager.h"

// Non-Windows stub: the application is Windows-only, but the target still
// builds elsewhere so that the core and UI can be compiled and tested.

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
}

HotkeyManager::~HotkeyManager() = default;

bool HotkeyManager::registerHotkey(HotkeyAction action, const QKeySequence &sequence)
{
    Q_UNUSED(action);
    Q_UNUSED(sequence);
    return false;
}

void HotkeyManager::unregisterHotkey(HotkeyAction action)
{
    Q_UNUSED(action);
}

void HotkeyManager::unregisterAll()
{
}

void HotkeyManager::setSuspended(bool suspended)
{
    m_suspended = suspended;
}

QKeySequence HotkeyManager::hotkey(HotkeyAction action) const
{
    Q_UNUSED(action);
    return {};
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message,
                                      qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
}

bool HotkeyManager::registerNative(HotkeyAction action, Registration &registration)
{
    Q_UNUSED(action);
    Q_UNUSED(registration);
    return false;
}

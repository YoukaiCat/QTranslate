#include "platform/hotkeymanager.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#include <windows.h>

Q_LOGGING_CATEGORY(lcHotkey, "qtranslate.hotkey", QtWarningMsg)

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace {

QString winErrorString(DWORD error)
{
    wchar_t *buffer = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                           | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (length > 0 && buffer)
        text = QString::fromWCharArray(buffer, int(length)).trimmed();
    if (buffer)
        LocalFree(buffer);
    if (text.isEmpty())
        text = QStringLiteral("error %1").arg(error);
    return text;
}

} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<HotkeyAction>("HotkeyAction");
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool HotkeyManager::registerHotkey(HotkeyAction action, const QKeySequence &sequence)
{
    const HotkeySequence::Hotkey hotkey = HotkeySequence::fromKeySequence(sequence);
    if (!hotkey.isValid()) {
        const QString reason = tr("The key combination is not supported.");
        qCWarning(lcHotkey) << "Unsupported hotkey" << sequence.toString(QKeySequence::NativeText);
        emit registrationFailed(action, sequence, reason);
        return false;
    }

    Registration &registration = m_registrations[int(action)];
    if (registration.registered) {
        UnregisterHotKey(nullptr, idForAction(action));
        registration.registered = false;
    }
    registration.sequence = sequence;
    registration.hotkey = hotkey;
    m_actionsById.insert(idForAction(action), action);

    if (m_suspended) {
        qCInfo(lcHotkey) << "Suspended; remembering"
                         << sequence.toString(QKeySequence::NativeText);
        return true;
    }
    return registerNative(action, registration);
}

void HotkeyManager::unregisterHotkey(HotkeyAction action)
{
    auto it = m_registrations.find(int(action));
    if (it == m_registrations.end())
        return;
    if (it->registered)
        UnregisterHotKey(nullptr, idForAction(action));
    m_actionsById.remove(idForAction(action));
    m_registrations.erase(it);
}

void HotkeyManager::unregisterAll()
{
    for (auto it = m_registrations.begin(); it != m_registrations.end(); ++it) {
        if (it->registered)
            UnregisterHotKey(nullptr, idForAction(HotkeyAction(it.key())));
    }
    m_registrations.clear();
    m_actionsById.clear();
}

void HotkeyManager::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;

    if (suspended) {
        for (auto it = m_registrations.begin(); it != m_registrations.end(); ++it) {
            if (it->registered) {
                UnregisterHotKey(nullptr, idForAction(HotkeyAction(it.key())));
                it->registered = false;
            }
        }
        qCInfo(lcHotkey) << "Hotkeys suspended";
        return;
    }

    const QList<int> actions = m_registrations.keys();
    for (int action : actions) {
        auto it = m_registrations.find(action);
        if (it != m_registrations.end() && !it->registered)
            registerNative(HotkeyAction(action), *it);
    }
    qCInfo(lcHotkey) << "Hotkeys resumed";
}

QKeySequence HotkeyManager::hotkey(HotkeyAction action) const
{
    const auto it = m_registrations.constFind(int(action));
    return it == m_registrations.cend() ? QKeySequence() : it->sequence;
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message,
                                      qintptr *result)
{
    if (eventType != QByteArrayLiteral("windows_generic_MSG")
        && eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
        return false;
    }

    const MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_HOTKEY)
        return false;

    const auto it = m_actionsById.constFind(int(msg->wParam));
    if (it == m_actionsById.cend())
        return false;

    if (result)
        *result = 0;
    qCDebug(lcHotkey) << "Hotkey activated:" << int(it.value());
    emit activated(it.value());
    return true;
}

int HotkeyManager::idForAction(HotkeyAction action)
{
    return 1 + int(action); // RegisterHotKey ids 0x0001..0xBFFF
}

bool HotkeyManager::registerNative(HotkeyAction action, Registration &registration)
{
    const int id = idForAction(action);
    const UINT modifiers = UINT(registration.hotkey.modifiers) | MOD_NOREPEAT;
    if (!RegisterHotKey(nullptr, id, modifiers, UINT(registration.hotkey.virtualKey))) {
        const DWORD error = GetLastError();
        registration.registered = false;
        const QString reason = winErrorString(error);
        qCWarning(lcHotkey) << "Cannot register" << registration.sequence.toString(QKeySequence::NativeText)
                            << "for action" << int(action) << ":" << reason;
        emit registrationFailed(action, registration.sequence, reason);
        return false;
    }

    registration.registered = true;
    qCInfo(lcHotkey) << "Registered" << registration.sequence.toString(QKeySequence::NativeText)
                     << "for action" << int(action);
    return true;
}

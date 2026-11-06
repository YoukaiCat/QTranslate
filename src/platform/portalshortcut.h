#pragma once

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

// One entry of the GlobalShortcuts `a(sa{sv})` argument: the stable shortcut ID
// and its options (description, preferred_trigger, trigger_description, ...).
// The portal interface uses the same shape for BindShortcuts, its response, and
// ShortcutsChanged, so the application and the mock portal in the test share
// these operators; duplicating the type in the test would be an ODR hazard.
struct PortalShortcut
{
    QString id;
    QVariantMap options;
};

// QtDBus derives the D-Bus signature from these operators, which is why they
// must bracket the members with beginStructure/endStructure.
inline QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.options;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument,
                                       PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.options;
    argument.endStructure();
    return argument;
}

Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(QList<PortalShortcut>)

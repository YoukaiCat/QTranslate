#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;

// Ensures only one instance of the application runs per user.
//
// The primary instance is elected with a lock file (atomic and robust against
// crashes: a stale lock of a dead process is taken over). The primary also runs
// a QLocalServer; secondary instances connect to it to pass a command and exit.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    enum class Command {
        None,     // just exit (e.g. a second autostart with --minimized)
        Activate, // show the main window of the running instance
    };

    // `key` identifies the instance group; use defaultKey() normally.
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);
    ~SingleInstance() override;

    // Per-user key: "qtranslate-<user name>".
    static QString defaultKey();

    // Returns true if this process became the primary instance.
    // Otherwise sends `command` to the running instance and returns false;
    // the caller should exit. Blocks for at most ~1 s, so call it at startup only.
    bool tryBecomePrimary(Command command);

signals:
    // Emitted in the primary instance when another instance asks to activate.
    void activateRequested();

private:
    void onNewConnection();
    bool sendToPrimary(Command command);

    QString m_serverName;
    QLockFile m_lock;
    std::unique_ptr<QLocalServer> m_server;
};

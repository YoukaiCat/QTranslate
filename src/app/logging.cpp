#include "app/logging.h"

#include "core/apppaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>

Q_LOGGING_CATEGORY(lcApp, "qtranslate.app", QtWarningMsg)

namespace {

QMutex g_mutex;
QFile g_file;
QString g_path;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO ";
    case QtWarningMsg:
        return "WARN ";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "?????";
}

void fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QMutexLocker locker(&g_mutex);
    if (!g_file.isOpen())
        return;

    const QString line = QStringLiteral("%1 %2 %3: %4\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QLatin1String(levelName(type)),
                                  QLatin1String(context.category ? context.category : "default"),
                                  message);
    g_file.write(line.toUtf8());
    g_file.flush();
}

} // namespace

namespace Logging {

void initialize(bool verbose)
{
    if (!verbose)
        return;

    const QString directory = AppPaths::stateDirectory();
    if (!QDir().mkpath(directory)) {
        qWarning("Cannot create log directory %s", qPrintable(directory));
        return;
    }

    g_path = QDir(directory).filePath(QStringLiteral("qtranslate.log"));
    constexpr qint64 kMaxLogSize = 5 * 1024 * 1024;
    if (QFileInfo(g_path).size() > kMaxLogSize) {
        const QString previous = g_path + QStringLiteral(".1");
        QFile::remove(previous);
        QFile::rename(g_path, previous);
    }

    g_file.setFileName(g_path);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning("Cannot open log file %s", qPrintable(g_path));
        return;
    }

    QLoggingCategory::setFilterRules(QStringLiteral("qtranslate.*=true"));
    qInstallMessageHandler(fileMessageHandler);
}

QString filePath()
{
    return g_path;
}

} // namespace Logging

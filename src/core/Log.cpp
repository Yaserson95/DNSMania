#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QMutex g_mutex;
QFile g_file;

// Все qDebug/qWarning/qCritical включаются в лог-файл.
void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO"; break;
    case QtWarningMsg:  level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg:    level = "FATAL"; break;
    }
    QString line = message;
    if (context.file)
        line += QStringLiteral(" [%1:%2]").arg(QLatin1String(context.file)).arg(context.line);
    Log::write(QLatin1String(level), line);
    if (type == QtFatalMsg)
        abort();
}

} // namespace

namespace Log {

QString filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return dir + QStringLiteral("/dnsmania.log");
}

void init()
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Ротация: при очень большом логе сохраняем прошлую копию.
    const QFileInfo fi(path);
    if (fi.exists() && fi.size() > 5 * 1024 * 1024) {
        const QString old = path + QStringLiteral(".old");
        QFile::remove(old);
        QFile::rename(path, old);
    }

    g_file.setFileName(path);
    if (!g_file.open(QIODevice::Append | QIODevice::Text))
        return; // лог недоступен — работаем без него

    qInstallMessageHandler(&qtMessageHandler);
}

void write(const QString& level, const QString& message)
{
    QMutexLocker lock(&g_mutex);
    if (!g_file.isOpen())
        return;
    QTextStream ts(&g_file);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
       << QLatin1Char(' ') << level << QLatin1Char(' ') << message << QLatin1Char('\n');
    ts.flush();
}

} // namespace Log

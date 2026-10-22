#pragma once

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QMutex>
#include <QTextStream>

#define SONO_LOG qInfo().noquote() << "[Sono]" << __FUNCTION__ << "|"
#define SONO_WARN qWarning().noquote() << "[Sono]" << __FUNCTION__ << "|"
#define SONO_ERR qCritical().noquote() << "[Sono]" << __FUNCTION__ << "|"

inline QFile *sonoLogFile()
{
    static QFile file;
    return &file;
}

inline void sonoMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx);
    const char *lvl = "INFO";
    if (type == QtWarningMsg)
        lvl = "WARN";
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        lvl = "ERR ";
    else if (type == QtDebugMsg)
        lvl = "DBG ";
    const QString line = QStringLiteral("[%1] %2 %3\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")),
                                  QLatin1String(lvl), msg);
    QFile *file = sonoLogFile();
    if (file && file->isOpen()) {
        static QMutex mutex;
        QMutexLocker lock(&mutex);
        file->write(line.toUtf8());
        file->flush();
    }
    fprintf(stderr, "%s", qUtf8Printable(line));
}

inline void sonoStartLog(const QString &path)
{
    QFile *file = sonoLogFile();
    if (file->isOpen())
        file->close();
    file->setFileName(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        fprintf(stderr, "[Sono] failed to open log %s\n", qUtf8Printable(path));
    qInstallMessageHandler(sonoMessageHandler);
    SONO_LOG << "log file" << path;
}

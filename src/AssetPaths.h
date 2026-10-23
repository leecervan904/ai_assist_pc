#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

inline QString findAsset(const QString &name)
{
    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        app.filePath(name),
        QDir(app.absoluteFilePath(QStringLiteral(".."))).filePath(name),
        QStringLiteral("C:/Users/obliv/Desktop/Code/sono-workstation/") + name,
        QStringLiteral("C:/Users/obliv/Desktop/Bee/bee-ultrasound-player/android/src/main/assets/ship/thyroid/") + name,
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QFileInfo(path).absoluteFilePath();
    }
    return app.filePath(name);
}

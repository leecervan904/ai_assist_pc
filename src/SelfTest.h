#pragma once

#include "AppMode.h"

#include <QJsonArray>
#include <QObject>
#include <QVariantMap>

class CapturePipeline;
class HostBridge;
class MainWindow;

class SelfTest : public QObject
{
    Q_OBJECT

public:
    SelfTest(MainWindow *window, CapturePipeline *capture, HostBridge *bridge, AppMode mode,
             QObject *parent = nullptr);
    void start();

signals:
    void finished(bool ok);

private:
    void advance();
    void check(const QString &name, bool ok, const QString &detail = {});
    void finish();
    QString cameraId() const;

    MainWindow *m_window = nullptr;
    CapturePipeline *m_capture = nullptr;
    HostBridge *m_bridge = nullptr;
    AppMode m_mode = AppMode::WebUnderlay;
    int m_step = 0;
    int m_passed = 0;
    int m_failed = 0;
    QJsonArray m_checks;
    QVariantMap m_lastAi;
    int m_lastCapFps = 0;
    int m_cineCount = 0;
    int m_cineView = -1;
    bool m_gotInfer = false;
    bool m_finished = false;
};

#include "SelfTest.h"

#include "CapturePipeline.h"
#include "HostBridge.h"
#include "Log.h"
#include "MainWindow.h"
#include "PreviewSurface.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSize>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

SelfTest::SelfTest(MainWindow *window, CapturePipeline *capture, HostBridge *bridge, AppMode mode,
                   QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_capture(capture)
    , m_bridge(bridge)
    , m_mode(mode)
{
    connect(m_bridge, &HostBridge::aiUpdated, this, [this](const QVariantMap &ai) {
        m_lastAi = ai;
        if (!ai.isEmpty())
            m_gotInfer = true;
        SONO_LOG << "aiUpdated boxes"
                 << ai.value(QStringLiteral("boxes")).toList().size();
    });
    connect(m_bridge, &HostBridge::ratesUpdated, this, [this](int cap, int infer) {
        m_lastCapFps = cap;
        SONO_LOG << "rates capture" << cap << "infer" << infer;
    });
    connect(m_bridge, &HostBridge::cineCountChanged, this, [this](int n) {
        m_cineCount = n;
        SONO_LOG << "cineCount" << n;
    });
    connect(m_bridge, &HostBridge::cineViewChanged, this, [this](int i) {
        m_cineView = i;
        SONO_LOG << "cineView" << i;
    });
    connect(m_bridge, &HostBridge::captureError, this, [](const QString &msg) {
        SONO_WARN << "captureError" << msg;
    });
}

void SelfTest::start()
{
    SONO_LOG << "self-test begin mode" << appModeName(m_mode);
    QTimer::singleShot(2800, this, &SelfTest::advance);
}

void SelfTest::check(const QString &name, bool ok, const QString &detail)
{
    QJsonObject row;
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("ok"), ok);
    row.insert(QStringLiteral("detail"), detail);
    m_checks.push_back(row);
    if (ok) {
        ++m_passed;
        SONO_LOG << "PASS" << name << detail;
    } else {
        ++m_failed;
        SONO_ERR << "FAIL" << name << detail;
    }
}

QString SelfTest::cameraId() const
{
    for (const QVariant &item : m_bridge->devices()) {
        const QVariantMap dev = item.toMap();
        const QString id = dev.value(QStringLiteral("id")).toString();
        if (!id.startsWith(QLatin1String("file:")))
            return id;
    }
    return {};
}

void SelfTest::advance()
{
    switch (m_step++) {
    case 0: {
        SONO_LOG << "step list devices";
        const QVariantList devices = m_bridge->devices();
        check(QStringLiteral("list-devices"), !devices.isEmpty(),
              QStringLiteral("count=%1").arg(devices.size()));
        bool hasThyroid = false;
        for (const QVariant &item : devices) {
            if (item.toMap().value(QStringLiteral("id")).toString() == QLatin1String("file:thyroid"))
                hasThyroid = true;
        }
        check(QStringLiteral("test-video-thyroid"), hasThyroid,
              QStringLiteral("C:/Users/obliv/Desktop/我的甲状腺.mp4"));
        QTimer::singleShot(200, this, &SelfTest::advance);
        break;
    }
    case 1: {
        SONO_LOG << "step start file video";
        m_window->showBenchPreview();
        m_window->evalJs(QStringLiteral(
            "if (typeof window.__sonoBenchStart==='function') window.__sonoBenchStart();"));
        const QVariantMap started = m_bridge->start(QStringLiteral("file:thyroid"), 1920, 1080, 30);
        check(QStringLiteral("start-file"), started.value(QStringLiteral("ok")).toBool(),
              started.value(QStringLiteral("error")).toString().isEmpty()
                  ? QStringLiteral("%1x%2")
                        .arg(started.value(QStringLiteral("width")).toInt())
                        .arg(started.value(QStringLiteral("height")).toInt())
                  : started.value(QStringLiteral("error")).toString());
        QTimer::singleShot(1800, this, &SelfTest::advance);
        break;
    }
    case 2: {
        const QSize sz = m_capture->latestSize();
        const bool running = m_capture->isRunning();
        check(QStringLiteral("file-running"), running,
              QStringLiteral("size=%1x%2 fps=%3").arg(sz.width()).arg(sz.height()).arg(m_lastCapFps));
        check(QStringLiteral("file-preview-size"), sz.width() >= 16 && sz.height() >= 16,
              QStringLiteral("%1x%2").arg(sz.width()).arg(sz.height()));
        QTimer::singleShot(200, this, &SelfTest::advance);
        break;
    }
    case 3: {
        SONO_LOG << "step default ROI + inference on";
        m_bridge->setRoi(true, 0, 0, 640, 640);
        m_bridge->setDetectModel(QStringLiteral("bee-thyroid"));
        m_gotInfer = false;
        m_lastAi.clear();
        m_bridge->setInference(true);
        m_window->evalJs(QStringLiteral(
            "if (typeof window.__sonoVerifyAi==='function') window.__sonoVerifyAi();"));
        QTimer::singleShot(2500, this, &SelfTest::advance);
        break;
    }
    case 4: {
        const QVariantList boxes = m_lastAi.value(QStringLiteral("boxes")).toList();
        check(QStringLiteral("inference-result"), m_gotInfer,
              QStringLiteral("boxes=%1 model=%2")
                  .arg(boxes.size())
                  .arg(m_lastAi.value(QStringLiteral("model")).toString()));
        QTimer::singleShot(200, this, &SelfTest::advance);
        break;
    }
    case 5: {
        SONO_LOG << "step stop AI clears boxes";
        m_bridge->setInference(false);
        const QVariantList boxes = m_lastAi.value(QStringLiteral("boxes")).toList();
        check(QStringLiteral("stop-ai-clears-boxes"), boxes.isEmpty(),
              QStringLiteral("boxes=%1").arg(boxes.size()));
        QTimer::singleShot(200, this, &SelfTest::advance);
        break;
    }
    case 6: {
        SONO_LOG << "step freeze + cine";
        m_bridge->setFrozen(true);
        m_window->evalJs(QStringLiteral(
            "if (typeof window.__sonoVerifyCinePlay==='function') window.__sonoVerifyCinePlay();"));
        QTimer::singleShot(1600, this, &SelfTest::advance);
        break;
    }
    case 7: {
        check(QStringLiteral("freeze-cine-count"), m_cineCount > 1 || m_capture->cineCount() > 1,
              QStringLiteral("count=%1").arg(qMax(m_cineCount, m_capture->cineCount())));
        check(QStringLiteral("cine-play"), m_capture->hasFilePlayer() || m_cineView >= 0,
              QStringLiteral("view=%1 file=%2").arg(m_cineView).arg(m_capture->hasFilePlayer()));
        m_bridge->setCinePlaying(false, 0, -1);
        m_bridge->setCineIndex(0);
        QTimer::singleShot(400, this, &SelfTest::advance);
        break;
    }
    case 10: {
        SONO_LOG << "step unfreeze + disconnect";
        m_bridge->setFrozen(false);
        m_bridge->stop();
        const bool hidden = m_window->previewSurface() && !m_window->previewSurface()->isVisible();
        check(QStringLiteral("disconnect-hides-preview"), hidden, {});
        check(QStringLiteral("disconnect-stops-capture"), !m_capture->isRunning(), {});
        QTimer::singleShot(400, this, &SelfTest::advance);
        break;
    }
    case 11: {
        const QString cam = cameraId();
        if (cam.isEmpty()) {
            check(QStringLiteral("capture-card"), true, QStringLiteral("skipped-no-camera"));
            QTimer::singleShot(200, this, &SelfTest::advance);
            break;
        }
        SONO_LOG << "step capture card" << cam;
        m_window->showBenchPreview();
        m_lastCapFps = 0;
        const QVariantMap started = m_bridge->start(cam, 1280, 720, 240);
        check(QStringLiteral("start-capture-card"), started.value(QStringLiteral("ok")).toBool(),
              QStringLiteral("%1 %2x%3 @%4")
                  .arg(started.value(QStringLiteral("name")).toString(),
                       started.value(QStringLiteral("width")).toString(),
                       started.value(QStringLiteral("height")).toString(),
                       started.value(QStringLiteral("fps")).toString()));
        QTimer::singleShot(2500, this, &SelfTest::advance);
        break;
    }
    case 12: {
        const QString cam = cameraId();
        if (!cam.isEmpty()) {
            check(QStringLiteral("capture-card-frames"), m_capture->isRunning() && m_lastCapFps > 0,
                  QStringLiteral("fps=%1 size=%2x%3")
                      .arg(m_lastCapFps)
                      .arg(m_capture->latestSize().width())
                      .arg(m_capture->latestSize().height()));
            m_bridge->stop();
        }
        finish();
        break;
    }
    default:
        finish();
        break;
    }
}

void SelfTest::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    QJsonObject obj;
    obj.insert(QStringLiteral("mode"), appModeName(m_mode));
    obj.insert(QStringLiteral("passed"), m_passed);
    obj.insert(QStringLiteral("failed"), m_failed);
    obj.insert(QStringLiteral("ok"), m_failed == 0);
    obj.insert(QStringLiteral("checks"), m_checks);
    obj.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    const QString path = QCoreApplication::applicationDirPath()
        + QStringLiteral("/selftest-") + appModeName(m_mode) + QStringLiteral(".json");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    SONO_LOG << "self-test written" << path << "passed" << m_passed << "failed" << m_failed;
    emit finished(m_failed == 0);
}

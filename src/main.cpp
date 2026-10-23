#include "AppMode.h"
#include "CapturePipeline.h"
#include "CefHost.h"
#include "HostBridge.h"
#include "Log.h"
#include "MainWindow.h"
#include "PreviewSurface.h"
#include "SelfTest.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <algorithm>

static QString findWorkstationHtml()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.filePath(QStringLiteral("web/workstation.html")),
        QDir(appDir.absoluteFilePath(QStringLiteral(".."))).filePath(QStringLiteral("web/workstation.html")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QFileInfo(path).absoluteFilePath();
    }
    return {};
}

int main(int argc, char *argv[])
{
    const int cefCode = sonoCefExecuteProcess(argc, argv);
    if (cefCode >= 0)
        return cefCode;

    QCoreApplication::setOrganizationName(QStringLiteral("SonoMind"));
    QCoreApplication::setApplicationName(QStringLiteral("SonoWorkstation"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "windows");
#endif
    qputenv("QSG_RHI_PRESENT_INTERVAL", "0");
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("超声智能工作站"));
    parser.addHelpOption();
    parser.addOption({{"b", "bench"},
                      QStringLiteral("自动连接采集卡并采样 FPS 后退出（秒）"),
                      QStringLiteral("seconds"),
                      QStringLiteral("0")});
    parser.addOption({{"s", "size"},
                      QStringLiteral("bench 分辨率 WxH"),
                      QStringLiteral("size"),
                      QStringLiteral("1280x720")});
    parser.addOption({QStringLiteral("verify"),
                      QStringLiteral("自动打开测试视频并展开 AI 菜单")});
    parser.process(app);
    const AppMode mode = AppMode::WebUnderlay;
    sonoCefInitialize(QCoreApplication::applicationDirPath(), argc, argv);
    const int benchSec = std::max(0, parser.value(QStringLiteral("bench")).toInt());
    int benchW = 1280;
    int benchH = 720;
    {
        const QStringList wh = parser.value(QStringLiteral("size")).toLower().split(QLatin1Char('x'));
        if (wh.size() == 2) {
            benchW = std::max(16, wh[0].toInt());
            benchH = std::max(16, wh[1].toInt());
        }
    }

    const QString htmlPath = findWorkstationHtml();
    if (htmlPath.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("超声智能工作站"),
                              QStringLiteral("找不到工作站页面 workstation.html"));
        return 1;
    }

    sonoStartLog(QCoreApplication::applicationDirPath() + QStringLiteral("/sono.log"));
    SONO_LOG << "html" << htmlPath << "mode" << appModeName(mode)
             << "verify" << parser.isSet(QStringLiteral("verify"))
             << "bench" << benchSec << "size" << benchW << "x" << benchH;

    CapturePipeline capture;

    const QUrl pageUrl = QUrl::fromLocalFile(htmlPath);
    MainWindow window(&capture, pageUrl, mode);
    window.resize(parser.isSet(QStringLiteral("verify")) ? 1600 : 1440,
                  parser.isSet(QStringLiteral("verify")) ? 1050 : 900);
    window.show();
    SONO_LOG << "main window shown";

#ifdef Q_OS_MACOS
    if (mode == AppMode::WebUnderlay) {
        auto *cefPump = new QTimer(&window);
        cefPump->setInterval(4);
        QObject::connect(cefPump, &QTimer::timeout, cefPump,
                         []() { sonoCefMessageLoopWork(); });
        cefPump->start();
    }
#endif

    if (parser.isSet(QStringLiteral("verify"))) {
        auto *selfTest = new SelfTest(&window, &capture, window.hostBridge(), mode, &window);
        QObject::connect(selfTest, &SelfTest::finished, &app, [](bool ok) {
            SONO_LOG << "self-test finished ok" << ok;
            QCoreApplication::exit(ok ? 0 : 2);
        });
        selfTest->start();
    }

    if (benchSec > 0) {
        struct Sample { int cap = 0, disp = 0, infer = 0, lat = 0; };
        auto *samples = new QVector<Sample>;
        QObject::connect(&window, &MainWindow::fpsHud, &window,
                         [samples](int cap, int disp, int infer, int lat) {
                             samples->push_back({cap, disp, infer, lat});
                         });

        QString sourceName = QStringLiteral("XT 测试视频");
        QString sourceId = QStringLiteral("file:xt");
        int sourceFps = 30;
        const QVariantList devices = capture.listDevices();
        for (const QVariant &item : devices) {
            const QVariantMap dev = item.toMap();
            const QString id = dev.value(QStringLiteral("id")).toString();
            if (id.startsWith(QLatin1String("file:")))
                continue;
            int matchedFps = 0;
            for (const QVariant &modeVar : dev.value(QStringLiteral("modes")).toList()) {
                const QVariantMap m = modeVar.toMap();
                if (m.value(QStringLiteral("width")).toInt() != benchW
                    || m.value(QStringLiteral("height")).toInt() != benchH)
                    continue;
                int best = 0;
                for (const QVariant &r : m.value(QStringLiteral("fps")).toList())
                    best = std::max(best, r.toInt());
                if (best > matchedFps) {
                    sourceId = id;
                    sourceName = dev.value(QStringLiteral("name")).toString();
                    sourceFps = best;
                    matchedFps = best;
                }
            }
            if (matchedFps > 0)
                break;
        }

        QTimer::singleShot(4000, &window,
                           [&window, &capture, sourceId, sourceFps, benchW, benchH]() {
            window.showBenchPreview();
            capture.setDisplay(window.previewSurface()->videoOutput(),
                               window.previewSurface()->displaySink());
            capture.startDevice(sourceId, benchW, benchH, sourceFps);
            window.evalJs(QStringLiteral(
                "if (typeof window.__sonoBenchStart==='function') window.__sonoBenchStart();"));
        });
        QTimer::singleShot((5 + benchSec) * 1000, &window,
                           [&window, &capture, mode, sourceId, sourceName, sourceFps, samples, benchW, benchH]() {
            capture.stopDevice();
            int capSum = 0, dispSum = 0, inferSum = 0, latSum = 0, n = 0;
            int capMax = 0, dispMax = 0, inferMax = 0, latMax = 0;
            for (const Sample &s : *samples) {
                if (s.cap <= 0 && s.disp <= 0)
                    continue;
                capSum += s.cap;
                dispSum += s.disp;
                inferSum += s.infer;
                latSum += s.lat;
                capMax = std::max(capMax, s.cap);
                dispMax = std::max(dispMax, s.disp);
                inferMax = std::max(inferMax, s.infer);
                latMax = std::max(latMax, s.lat);
                ++n;
            }
            QJsonObject obj;
            obj.insert(QStringLiteral("mode"), appModeName(mode));
            obj.insert(QStringLiteral("sourceId"), sourceId);
            obj.insert(QStringLiteral("sourceName"), sourceName);
            const QString requested = QStringLiteral("%1x%2").arg(benchW).arg(benchH);
            obj.insert(QStringLiteral("requested"), requested);
            obj.insert(QStringLiteral("sourceFps"), sourceFps);
            obj.insert(QStringLiteral("samples"), n);
            obj.insert(QStringLiteral("captureAvg"), n ? capSum / n : 0);
            obj.insert(QStringLiteral("displayAvg"), n ? dispSum / n : 0);
            obj.insert(QStringLiteral("inferAvg"), n ? inferSum / n : 0);
            obj.insert(QStringLiteral("captureMax"), capMax);
            obj.insert(QStringLiteral("displayMax"), dispMax);
            obj.insert(QStringLiteral("inferMax"), inferMax);
            obj.insert(QStringLiteral("latencyAvgMs"), n ? latSum / n : 0);
            obj.insert(QStringLiteral("latencyMaxMs"), latMax);
            obj.insert(QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate));
            QString file = QStringLiteral("/bench-") + appModeName(mode);
            if (benchW != 1920 || benchH != 1080)
                file += QLatin1Char('-') + requested;
            const QString path = QCoreApplication::applicationDirPath() + file + QStringLiteral(".json");
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
            qInfo() << "bench written" << path << obj;
            QCoreApplication::quit();
        });
    }

    const int exitCode = app.exec();
    sonoCefShutdown();
    return exitCode;
}

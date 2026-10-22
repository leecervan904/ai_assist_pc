#include "MainWindow.h"

#include "CapturePipeline.h"
#include "HostBridge.h"
#include "Log.h"
#include "PreviewWidget.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QtGlobal>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QSize>
#include <QStatusBar>
#include <QWidget>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

namespace {

std::string sonoOk(const QVariant &value)
{
    const QJsonObject obj{
        {QStringLiteral("ok"), true},
        {QStringLiteral("result"), QJsonValue::fromVariant(value)}};
    return std::string(
        QJsonDocument(obj).toJson(QJsonDocument::Compact).constData());
}

std::string sonoError(const QString &message)
{
    const QJsonObject obj{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message}};
    return std::string(
        QJsonDocument(obj).toJson(QJsonDocument::Compact).constData());
}

}  // namespace

class OverlayWindow final : public QWidget
{
public:
    explicit OverlayWindow(QWidget *parent)
        : QWidget(parent,
                  Qt::Tool | Qt::FramelessWindowHint
                      | Qt::NoDropShadowWindowHint
#ifdef Q_OS_MACOS
                      | Qt::WindowDoesNotAcceptFocus
#endif
#if defined(Q_OS_LINUX) && defined(SONO_X11_BYPASS_WM)
                      | Qt::X11BypassWindowManagerHint
#endif
                  )
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
        setAutoFillBackground(false);
        hide();
    }

    using MouseMoveCb = std::function<void(QPoint, bool)>;
    using MouseButtonCb = std::function<void(QPoint, int, bool, int)>;
    using MouseWheelCb = std::function<void(QPoint, int, int)>;

    void setMouseCallbacks(MouseMoveCb moveCb, MouseButtonCb buttonCb,
                           MouseWheelCb wheelCb)
    {
        m_moveCb = std::move(moveCb);
        m_buttonCb = std::move(buttonCb);
        m_wheelCb = std::move(wheelCb);
    }

    void setImage(const QImage &image)
    {
        m_image = image;
        update();
    }

protected:
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_moveCb)
            m_moveCb(event->pos(), false);
    }

    void leaveEvent(QEvent *) override
    {
        if (m_moveCb)
            m_moveCb(QPoint(), true);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_buttonCb)
            m_buttonCb(event->pos(), event->button(), true, 1);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_buttonCb)
            m_buttonCb(event->pos(), event->button(), false, 1);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_wheelCb)
            m_wheelCb(event->position().toPoint(), 0,
                      event->angleDelta().y());
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (m_image.isNull())
            return;
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(rect(), m_image);
    }

private:
    QImage m_image;
    MouseMoveCb m_moveCb;
    MouseButtonCb m_buttonCb;
    MouseWheelCb m_wheelCb;
};

MainWindow::MainWindow(CapturePipeline *capture, const QUrl &pageUrl, AppMode mode,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_mode(mode)
{
    SONO_LOG << "MainWindow ctor mode" << appModeName(mode);
    qApp->installEventFilter(this);
    setWindowTitle(QStringLiteral("超声智能工作站 [%1]").arg(appModeName(mode)));
    setMinimumSize(1100, 720);

    m_underlayHost = new QWidget(this);
    m_underlayHost->setStyleSheet(QStringLiteral("background: #0e0f12;"));
    setCentralWidget(m_underlayHost);
    auto *layout = new QVBoxLayout(m_underlayHost);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_preview = new PreviewWidget(m_underlayHost);
    m_preview->setWebUnderlay(true);
    m_preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_preview->hide();

    m_webOverlay = new OverlayWindow(m_underlayHost);
    m_webOverlay->hide();

    const QString htmlPath = pageUrl.toLocalFile();
    m_cefHost = new CefHost(
        [this](const QImage &frame) {
            if (!m_webOverlay)
                return;
            QMetaObject::invokeMethod(m_webOverlay, [this, frame]() {
                m_webOverlay->setImage(frame);
            }, Qt::QueuedConnection);
        },
        [this](bool ok) {
            SONO_LOG << "cef load" << ok;
        });
    const QSize webSize(1280, 900);
    m_cefHost->setViewSize(webSize);
    m_cefHost->createBrowser(
        QUrl::fromLocalFile(htmlPath).toString().toStdString(), webSize);
    m_webOverlay->setMouseCallbacks(
        [this](QPoint pos, bool leave) {
            if (m_cefHost)
                m_cefHost->sendMouseMove(pos, leave);
        },
        [this](QPoint pos, int button, bool down, int count) {
            if (m_cefHost)
                m_cefHost->sendMouseButton(pos, button, down, count);
        },
        [this](QPoint pos, int dx, int dy) {
            if (m_cefHost)
                m_cefHost->sendMouseWheel(pos, dx, dy);
        });

    m_preview->lower();

    m_preview->setDrawHud(false);
    if (auto *pw = qobject_cast<PreviewWidget *>(m_preview)) {
        pw->setCaptureMsProvider([capture]() { return capture->latestCaptureMs(); });
        pw->setCefUnderlay(m_cefHost != nullptr);
    }
    m_bridge = new HostBridge(capture, m_preview, this);
    m_bridge->setPreviewModeName(appModeName(mode));
    setupCefBridge();
    connect(m_bridge, &HostBridge::previewPlacement, this, &MainWindow::onPreviewPlacement);
    connect(m_bridge, &HostBridge::previewSize, this, [this](int w, int h) {
        m_srcW = w;
        m_srcH = h;
        refreshHud();
    });
    connect(capture, &CapturePipeline::previewFrame, m_preview, &PreviewSurface::present);
    connect(capture, &CapturePipeline::ratesUpdated, this, [this](int cap, int infer) {
        m_capFps = cap;
        m_inferFps = infer;
        m_displayFps = cap;
        refreshHud();
        if (m_bridge)
            m_bridge->notifyDisplayFps(cap);
    });
    connect(m_preview, &PreviewSurface::fpsUpdated, this, [this](int fps) {
        if (fps > 0)
            m_displayFps = fps;
        refreshHud();
        if (m_bridge)
            m_bridge->notifyDisplayFps(m_displayFps);
    });
    connect(m_bridge, &HostBridge::latencyUpdated, this, [this](int ms) {
        if (ms < 0 || ms > 8000)
            return;
        m_latencyMs = ms;
        refreshHud();
    });

    m_hud = new QLabel(this);
    statusBar()->addWidget(m_hud, 1);
    refreshHud();

}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_previewVisible) {
        const QSize sz = m_underlayHost ? m_underlayHost->size() : size();
        m_cssRect = QRect(0, 0,
                          sz.width() > 8 ? sz.width() : 1280,
                          sz.height() > 8 ? sz.height() : 900);
        m_previewVisible = true;
    }
    relayoutPreview();
}

void MainWindow::onPreviewPlacement(double x, double y, double w, double h, bool visible)
{
    SONO_LOG << "previewPlacement" << x << y << w << h << "visible" << visible;
    if (w <= 8 || h <= 8) {
        m_previewVisible = visible;
        relayoutPreview();
        return;
    }
    m_cssRect = QRect(int(x), int(y), int(w), int(h));
    m_previewVisible = visible;
    relayoutPreview();
}

void MainWindow::relayoutPreview()
{
    if (!m_underlayHost)
        return;

    if (isMinimized()) {
        m_preview->hide();
        if (m_webOverlay)
            m_webOverlay->hide();
        return;
    }

    const QRect pageRect = m_underlayHost->rect();
    const QPoint origin = m_underlayHost->mapToGlobal(QPoint(0, 0));
    if (pageRect.width() <= 8 || pageRect.height() <= 8) {
        m_preview->hide();
        if (m_webOverlay)
            m_webOverlay->hide();
        return;
    }

    if (m_previewVisible) {
        m_preview->setGeometry(m_cssRect);
        m_preview->show();
        m_preview->raise();
    } else {
        m_preview->hide();
    }

    if (m_cefHost)
        m_cefHost->setViewSize(pageRect.size());
    if (m_webOverlay) {
        m_webOverlay->setGeometry(QRect(origin, pageRect.size()));
        m_webOverlay->show();
        m_webOverlay->raise();
    }
}

void MainWindow::refreshHud()
{
    if (!m_hud)
        return;
    const QString res = (m_srcW > 0 && m_srcH > 0)
        ? QStringLiteral("%1x%2").arg(m_srcW).arg(m_srcH)
        : QStringLiteral("—");
    m_hud->setText(QStringLiteral("模式 %1    采集 %2 FPS    显示 %3 FPS    推理 %4 FPS    延迟 %5 ms    %6")
                       .arg(appModeName(m_mode))
                       .arg(m_capFps)
                       .arg(m_displayFps)
                       .arg(m_inferFps)
                       .arg(m_latencyMs)
                       .arg(res));
    emit fpsHud(m_capFps, m_displayFps, m_inferFps, m_latencyMs);
}

void MainWindow::showBenchPreview()
{
    m_cssRect = QRect(8, 80, 1280, 720);
    m_previewVisible = true;
    relayoutPreview();
}

void MainWindow::evalJs(const QString &js)
{
    Q_UNUSED(js);
}

void MainWindow::setupCefBridge()
{
    if (!m_cefHost)
        return;

    m_cefHost->setQueryHandler([this](const std::string &request) {
        std::string result;
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            result = handleSonoQuery(request);
        } else {
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [this, request, &result]() { result = handleSonoQuery(request); },
                Qt::BlockingQueuedConnection);
        }
        return result;
    });

    const auto push = [this](const QString &name, const QVariantList &args) {
        if (m_cefHost)
            m_cefHost->pushEvent(name, args);
    };

    connect(m_bridge, &HostBridge::aiUpdated, this,
            [push](const QVariantMap &ai) {
                push(QStringLiteral("aiUpdated"), QVariantList{ai});
            });
    connect(m_bridge, &HostBridge::previewSize, this,
            [push](int w, int h) {
                push(QStringLiteral("previewSize"), QVariantList{w, h});
            });
    connect(m_bridge, &HostBridge::ratesUpdated, this,
            [push](int cap, int infer) {
                push(QStringLiteral("ratesUpdated"), QVariantList{cap, infer});
            });
    connect(m_bridge, &HostBridge::displayFpsUpdated, this,
            [push](int fps) {
                push(QStringLiteral("displayFpsUpdated"), QVariantList{fps});
            });
    connect(m_bridge, &HostBridge::latencyUpdated, this,
            [push](int ms) {
                push(QStringLiteral("latencyUpdated"), QVariantList{ms});
            });
    connect(m_bridge, &HostBridge::cineCountChanged, this,
            [push](int count) {
                push(QStringLiteral("cineCountChanged"), QVariantList{count});
            });
    connect(m_bridge, &HostBridge::frameIdsChanged, this,
            [push](const QVariantList &ids) {
                push(QStringLiteral("frameIdsChanged"), QVariantList{ids});
            });
    connect(m_bridge, &HostBridge::cineViewChanged, this,
            [push](int index) {
                push(QStringLiteral("cineViewChanged"), QVariantList{index});
            });
    connect(m_bridge, &HostBridge::cinePlaybackEnded, this,
            [push]() { push(QStringLiteral("cinePlaybackEnded"), {}); });
    connect(m_bridge, &HostBridge::roiChanged, this,
            [push](int x, int y, int w, int h) {
                push(QStringLiteral("roiChanged"), QVariantList{x, y, w, h});
            });
    connect(m_bridge, &HostBridge::captureError, this,
            [push](const QString &msg) {
                push(QStringLiteral("captureError"), QVariantList{msg});
            });
}

std::string MainWindow::handleSonoQuery(const std::string &request)
{
    if (!m_bridge)
        return sonoError(QStringLiteral("native host 未就绪"));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(request), &parseError);
    if (parseError.error || !doc.isObject())
        return sonoError(QStringLiteral("无效请求: %1")
                             .arg(parseError.errorString()));

    const QJsonObject root = doc.object();
    const QString method = root.value(QStringLiteral("method")).toString();
    SONO_LOG << "sono query" << method;
    const QVariantList args =
        root.value(QStringLiteral("args")).toArray().toVariantList();
    const auto argStr = [&args](int i) {
        return i < args.size() ? args.at(i).toString() : QString();
    };
    const auto argInt = [&args](int i, int def = 0) {
        return i < args.size() ? args.at(i).toInt() : def;
    };
    const auto argBool = [&args](int i, bool def = false) {
        return i < args.size() ? args.at(i).toBool() : def;
    };
    const auto argDouble = [&args](int i, double def = 0.0) {
        return i < args.size() ? args.at(i).toDouble() : def;
    };

    if (method == QLatin1String("devices"))
        return sonoOk(m_bridge->devices());
    if (method == QLatin1String("start"))
        return sonoOk(m_bridge->start(argStr(0), argInt(1), argInt(2),
                                      argInt(3)));
    if (method == QLatin1String("pickVideoFile"))
        return sonoOk(m_bridge->pickVideoFile());
    if (method == QLatin1String("stop")) {
        m_bridge->stop();
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setInference")) {
        m_bridge->setInference(argBool(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setDetectModel"))
        return sonoOk(m_bridge->setDetectModel(argStr(0)));
    if (method == QLatin1String("detectModel"))
        return sonoOk(m_bridge->detectModel());
    if (method == QLatin1String("detectModels"))
        return sonoOk(m_bridge->detectModels());
    if (method == QLatin1String("inferOnce")) {
        m_bridge->inferOnce();
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setRoi")) {
        m_bridge->setRoi(argBool(0), argInt(1), argInt(2), argInt(3),
                         argInt(4));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setPreviewRect")) {
        m_bridge->setPreviewRect(argDouble(0), argDouble(1), argDouble(2),
                                 argDouble(3), argBool(4));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setFrozen")) {
        m_bridge->setFrozen(argBool(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setCineIndex")) {
        m_bridge->setCineIndex(argInt(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setCinePlaying")) {
        m_bridge->setCinePlaying(argBool(0), argInt(1), argInt(2));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setCineRingEnabled")) {
        m_bridge->setCineRingEnabled(argBool(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("cineCount"))
        return sonoOk(m_bridge->cineCount());
    if (method == QLatin1String("cineAi"))
        return sonoOk(m_bridge->cineAi(argInt(0)));
    if (method == QLatin1String("frameIds"))
        return sonoOk(m_bridge->frameIds());
    if (method == QLatin1String("status"))
        return sonoOk(m_bridge->status());
    if (method == QLatin1String("previewMode"))
        return sonoOk(m_bridge->previewMode());
    if (method == QLatin1String("notifyDisplayFps")) {
        m_bridge->notifyDisplayFps(argInt(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("notifyLatency")) {
        m_bridge->notifyLatency(argInt(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("setAiDrawMode")) {
        m_bridge->setAiDrawMode(argStr(0));
        return sonoOk(QVariant());
    }
    if (method == QLatin1String("aiDrawMode"))
        return sonoOk(m_bridge->aiDrawMode());
    return sonoError(QStringLiteral("未知方法: %1").arg(method));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_webOverlay && m_webOverlay->isVisible() && m_cefHost
        && (event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint local =
            m_webOverlay->mapFromGlobal(mouse->globalPosition().toPoint());
        const bool windowStage =
            QLatin1String(watched->metaObject()->className())
            == QLatin1String("QWidgetWindow");
        const bool fromOverlay =
            watched == m_webOverlay
            || (m_webOverlay->windowHandle()
                && watched == static_cast<QObject *>(m_webOverlay->windowHandle()));
        if (windowStage && !fromOverlay
            && m_webOverlay->rect().contains(local)) {
            if (event->type() == QEvent::MouseMove)
                m_cefHost->sendMouseMove(local, false);
            else
                m_cefHost->sendMouseButton(
                    local, static_cast<int>(Qt::LeftButton),
                    event->type() == QEvent::MouseButtonPress, 1);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::WindowActivate:
        relayoutPreview();
        QTimer::singleShot(0, this, [this]() { relayoutPreview(); });
        break;
    case QEvent::WindowDeactivate:
        if (m_webOverlay && !m_cefHost)
            m_webOverlay->hide();
        break;
    case QEvent::WindowStateChange:
        relayoutPreview();
        break;
    default:
        break;
    }
    return QMainWindow::event(event);
}

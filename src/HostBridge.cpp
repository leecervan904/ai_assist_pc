#include "HostBridge.h"

#include "CapturePipeline.h"
#include "Log.h"
#include "PreviewSurface.h"
#include "VideoFrameUtil.h"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QThread>
#include <QVideoFrame>
#include <QWidget>

HostBridge::HostBridge(CapturePipeline *capture, PreviewSurface *preview, QObject *parent)
    : QObject(parent)
    , m_capture(capture)
    , m_preview(preview)
{
    connect(m_capture, &CapturePipeline::inferenceResult, this, [this](const QVariantMap &ai) {
        emit aiUpdated(ai);
    });
    connect(m_capture, &CapturePipeline::captureError, this, &HostBridge::captureError);
    connect(m_capture, &CapturePipeline::frameSizeChanged, this, &HostBridge::previewSize);
    connect(m_capture, &CapturePipeline::ratesUpdated, this, &HostBridge::ratesUpdated);
    connect(m_capture, &CapturePipeline::cineCountChanged, this, [this](int count) {
        emit cineCountChanged(count);
        emit frameIdsChanged(m_capture ? m_capture->frameIds() : QVariantList{});
    });
    connect(m_capture, &CapturePipeline::cineViewChanged, this, [this](int index) {
        emit cineViewChanged(index);
        if (!m_preview || !m_capture || m_capture->hasFilePlayer())
            return;
        const QImage img = m_capture->cineImage(index);
        if (!img.isNull())
            m_preview->showStill(videoFrameFromImage(img));
        const QVariantMap ai = m_capture->cineAi(index);
        emit aiUpdated(ai);
    });
    connect(m_capture, &CapturePipeline::cinePlaybackEnded, this, &HostBridge::cinePlaybackEnded);
    if (m_preview) {
        m_preview->setDrawHud(false);
        connect(m_preview, &PreviewSurface::fpsUpdated, this, &HostBridge::displayFpsUpdated);
        connect(m_preview, &PreviewSurface::fpsUpdated, this, &HostBridge::fpsUpdated);
        connect(m_preview, &PreviewSurface::latencyUpdated, this, &HostBridge::latencyUpdated);
    }
}

HostBridge::~HostBridge()
{
}

QVariantList HostBridge::devices() const
{
    return m_capture ? m_capture->listDevices() : QVariantList{};
}

QVariantMap HostBridge::pickVideoFile()
{
    if (!m_capture)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("采集管线未就绪")}};

    QWidget *win = qobject_cast<QWidget *>(parent());
    if (win)
        win = win->window();
    if (!win)
        win = QApplication::activeWindow();

    const QString path = QFileDialog::getOpenFileName(
        win,
        QStringLiteral("选择测试视频"),
        m_lastVideoDir,
        QStringLiteral("视频 (*.mp4 *.avi *.mkv *.mov *.wmv *.mpeg *.mpg *.webm *.m4v *.ts);;所有文件 (*.*)"));
    if (path.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("cancelled"), true}};

    m_lastVideoDir = QFileInfo(path).absolutePath();
    return m_capture->addVideoFile(path);
}

QVariantMap HostBridge::start(const QString &id, int width, int height, int fps)
{
    if (!m_capture)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("采集管线未就绪")}};
    if (m_preview) {
        m_preview->setFrozen(false);
        m_preview->setHwPlayback(true);
        m_preview->setDrawHud(false);
        m_preview->clearFrame();
        m_capture->setDisplay(m_preview->videoOutput(), m_preview->displaySink());
    }
    SONO_LOG << "start" << id << width << "x" << height << "@" << fps
             << "videoOutput" << (m_preview ? m_preview->videoOutput() : nullptr)
             << "sink" << (m_preview ? m_preview->displaySink() : nullptr);
    const QVariantMap result = m_capture->startDevice(id, width, height, fps);
    SONO_LOG << "start result" << result;
    if (result.value(QStringLiteral("ok")).toBool()) {
        const int w = result.value(QStringLiteral("width")).toInt();
        const int h = result.value(QStringLiteral("height")).toInt();
        if (w > 0 && h > 0)
            emit previewSize(w, h);
    }
    return result;
}

void HostBridge::stop()
{
    SONO_LOG << "stop";
    if (m_capture) {
        m_capture->setInferenceEnabled(false);
        m_capture->stopDevice();
    }
    if (m_preview) {
        m_preview->setFrozen(false);
        m_preview->clearFrame();
        m_preview->hide();
    }
    emit previewPlacement(0, 0, 0, 0, false);
}

void HostBridge::setInference(bool on)
{
    SONO_LOG << "setInference" << on;
    if (m_capture)
        m_capture->setInferenceEnabled(on);
    if (!on) {
        emit aiUpdated(QVariantMap{});
    }
}

QVariantMap HostBridge::setDetectModel(const QString &id)
{
    return m_capture ? m_capture->setDetectModel(id)
                    : QVariantMap{{QStringLiteral("ok"), false}};
}

QString HostBridge::detectModel() const
{
    return m_capture ? m_capture->detectModel() : QString();
}

QVariantList HostBridge::detectModels() const
{
    return m_capture ? m_capture->detectModels() : QVariantList{};
}

void HostBridge::inferOnce()
{
    if (m_capture)
        m_capture->inferOnce();
}

void HostBridge::setPreviewModeName(const QString &name)
{
    m_modeName = name;
}

QString HostBridge::previewMode() const
{
    return m_modeName;
}

void HostBridge::notifyDisplayFps(int fps)
{
    emit displayFpsUpdated(fps);
    emit fpsUpdated(fps);
}

void HostBridge::notifyLatency(int latencyMs)
{
    emit latencyUpdated(latencyMs);
}

void HostBridge::setAiDrawMode(const QString &mode)
{
    Q_UNUSED(mode);
    if (m_aiDrawMode == QLatin1String("web"))
        return;
    m_aiDrawMode = QStringLiteral("web");
    if (m_preview)
        m_preview->setDrawHud(false);
    emit aiDrawModeChanged(m_aiDrawMode);
}

QString HostBridge::aiDrawMode() const
{
    return m_aiDrawMode;
}

void HostBridge::setRoi(bool enabled, int x, int y, int w, int h)
{
    SONO_LOG << "setRoi" << enabled << x << y << w << h;
    if (m_capture)
        m_capture->setRoi(enabled, x, y, w, h);
}

void HostBridge::setPreviewRect(double x, double y, double w, double h, bool visible)
{
    emit previewPlacement(x, y, w, h, visible);
}

void HostBridge::setFrozen(bool on)
{
    SONO_LOG << "setFrozen" << on << "filePlayer"
             << (m_capture && m_capture->hasFilePlayer());
    if (m_capture) {
        const bool filePlayer = m_capture->hasFilePlayer();
        m_capture->setFrozen(on);
        if (!filePlayer)
            m_capture->setDisplayPaused(on);
    }
    emit frozenChanged(on);
    if (!m_preview)
        return;
    m_preview->setFrozen(on);
    if (on && m_capture) {
        const bool filePlayer = m_capture->hasFilePlayer();
        m_preview->setHwPlayback(filePlayer);
        if (!filePlayer) {
            const QVideoFrame live = m_capture->latestFrame();
            if (live.isValid())
                m_preview->showStill(live);
        }
    } else {
        m_preview->setHwPlayback(true);
    }
}

void HostBridge::setCinePlaying(bool on, int from, int to)
{
    SONO_LOG << "setCinePlaying" << on << from << to;
    if (m_preview && m_capture)
        m_preview->setHwPlayback(m_capture->hasFilePlayer());
    if (m_capture)
        m_capture->setCinePlaying(on, from, to);
}

void HostBridge::setCineRingEnabled(bool on)
{
    if (m_capture)
        m_capture->setCineRingEnabled(on);
}

void HostBridge::setCineIndex(int index)
{
    if (!m_capture)
        return;
    m_capture->setCineIndex(index);
    if (!m_preview)
        return;
    if (m_capture->hasFilePlayer())
        return;
    const QImage img = m_capture->cineImage(index);
    if (!img.isNull())
        m_preview->showStill(videoFrameFromImage(img));
    const QVariantMap ai = m_capture->cineAi(index);
    emit aiUpdated(ai);
}

QVariantMap HostBridge::cineAi(int index) const
{
    return m_capture ? m_capture->cineAi(index) : QVariantMap{};
}

int HostBridge::cineCount() const
{
    return m_capture ? m_capture->cineCount() : 0;
}

QVariantList HostBridge::frameIds() const
{
    return m_capture ? m_capture->frameIds() : QVariantList{};
}

QString HostBridge::frameImageDataUrl(int id) const
{
    const QImage img = m_capture ? m_capture->frameImageById(id) : QImage();
    if (img.isNull())
        return {};
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!img.save(&buffer, "BMP"))
        return {};
    return QStringLiteral("data:image/bmp;base64,") + QString::fromLatin1(bytes.toBase64());
}

QVariantMap HostBridge::frameAi(int id) const
{
    return m_capture ? m_capture->frameAiById(id) : QVariantMap{};
}

void HostBridge::inferFrame(int id)
{
    if (m_capture)
        m_capture->inferFrameById(id);
}

QVariantMap HostBridge::status() const
{
    QVariantMap out;
    if (!m_capture)
        return out;
    const QSize sz = m_capture->latestSize();
    out.insert(QStringLiteral("running"), m_capture->isRunning());
    out.insert(QStringLiteral("width"), sz.width());
    out.insert(QStringLiteral("height"), sz.height());
    out.insert(QStringLiteral("ai"), m_capture->latestInference());
    return out;
}

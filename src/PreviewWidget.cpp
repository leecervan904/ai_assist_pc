#include "PreviewWidget.h"

#include "Log.h"
#include "VideoFrameUtil.h"

#include <QDateTime>
#include <QPainter>
#include <QResizeEvent>
#include <QVideoSink>
#include <QVideoWidget>

PreviewWidget::PreviewWidget(QWidget *parent)
    : PreviewSurface(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    m_drawHud = false;

    m_video = new QVideoWidget(this);
    m_video->setAspectRatioMode(Qt::KeepAspectRatio);
    m_video->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_video->setAttribute(Qt::WA_NativeWindow);
    m_video->show();
    layoutLayers();
}

PreviewWidget::~PreviewWidget()
{
}

QObject *PreviewWidget::videoOutput()
{
    return m_video;
}

QVideoSink *PreviewWidget::displaySink() const
{
    return m_video ? m_video->videoSink() : nullptr;
}

void PreviewWidget::layoutLayers()
{
    if (m_video)
        m_video->setGeometry(rect());
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
    PreviewSurface::resizeEvent(event);
    layoutLayers();
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

void PreviewWidget::syncHwVideo()
{
    const bool hw = m_webUnderlay ? true : (!m_frozen || m_hwPlayback);
    if (m_video)
        m_video->setVisible(hw);
}

void PreviewWidget::setWebUnderlay(bool on)
{
    m_webUnderlay = on;
    syncHwVideo();
}

void PreviewWidget::setCefUnderlay(bool on)
{
    m_cefUnderlay = on;
    syncHwVideo();
}

void PreviewWidget::setTransitionHold(bool on)
{
    m_transitionHold = on;
    syncHwVideo();
}

void PreviewWidget::setHwPlayback(bool on)
{
    m_hwPlayback = on;
    syncHwVideo();
}

void PreviewWidget::setFrozen(bool on)
{
    m_frozen = on;
    if (!on)
        m_hwPlayback = true;
    syncHwVideo();
}

void PreviewWidget::present(const QVideoFrame &frame, qint64 captureMs)
{
    if (!frame.isValid())
        return;
    m_frame = frame;
    m_still = QImage();
    if (m_frozen && m_webUnderlay && m_cefUnderlay && m_video)
        m_video->videoSink()->setVideoFrame(frame);
    reportLatency(captureMs);
    if (captureMs > 0) {
        const int ms = int(QDateTime::currentMSecsSinceEpoch() - captureMs);
        if (ms >= 0 && ms < 8000) {
            m_nativeFetchMsSum += ms;
            ++m_nativeFetchCount;
            const int avg = int(m_nativeFetchMsSum / m_nativeFetchCount);
            SONO_LOG << "native latency last" << ms << "ms avg" << avg
                     << "ms n" << m_nativeFetchCount;
        }
    }
    if (m_frozen && !m_hwPlayback)
        tickFps();
}

void PreviewWidget::showStill(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    m_frame = frame;
    m_still = frame.toImage();
    if (m_webUnderlay && m_cefUnderlay && m_video)
        m_video->videoSink()->setVideoFrame(frame);
    tickFps();
}

void PreviewWidget::clearFrame()
{
    m_frame = QVideoFrame();
    m_still = QImage();
    m_fps = 0;
    m_fpsCount = 0;
}

QSize PreviewWidget::frameSize() const
{
    return m_frame.isValid() ? m_frame.size() : QSize();
}

void PreviewWidget::tickFps()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ++m_fpsCount;
    if (m_fpsStartMs == 0)
        m_fpsStartMs = now;
    if (now - m_fpsStartMs >= 1000) {
        m_fps = m_fpsCount;
        m_fpsCount = 0;
        m_fpsStartMs = now;
        emit fpsUpdated(m_fps);
    }
}

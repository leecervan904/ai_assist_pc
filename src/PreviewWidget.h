#pragma once

#include "PreviewSurface.h"

#include <QImage>
#include <QSize>
#include <QVideoFrame>
#include <functional>

class QVideoWidget;

class PreviewWidget : public PreviewSurface
{
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    void present(const QVideoFrame &frame, qint64 captureMs = 0) override;
    void showStill(const QVideoFrame &frame) override;
    void clearFrame() override;
    void setCaptureMsProvider(const std::function<qint64()> &provider)
    {
        m_captureMsProvider = provider;
    }
    void setFrozen(bool on) override;
    void setHwPlayback(bool on) override;
    void setWebUnderlay(bool on) override;
    void setCefUnderlay(bool on);
    void setTransitionHold(bool on) override;
    QObject *videoOutput() override;
    QVideoSink *displaySink() const override;
    QSize frameSize() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutLayers();
    void syncHwVideo();
    void tickFps();

    QVideoWidget *m_video = nullptr;
    QVideoFrame m_frame;
    QImage m_still;
    bool m_frozen = false;
    bool m_hwPlayback = true;
    bool m_webUnderlay = false;
    bool m_cefUnderlay = false;
    bool m_transitionHold = false;
    std::function<qint64()> m_captureMsProvider;
    int m_fps = 0;
    int m_fpsCount = 0;
    qint64 m_fpsStartMs = 0;
    qint64 m_nativeFetchMsSum = 0;
    int m_nativeFetchCount = 0;
};

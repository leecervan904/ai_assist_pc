#pragma once

#include <QDateTime>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>

class PreviewSurface : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewSurface(QWidget *parent = nullptr)
        : QWidget(parent)
    {}

    virtual void present(const QVideoFrame &frame, qint64 captureMs = 0) = 0;
    virtual void showStill(const QVideoFrame &frame) = 0;
    virtual void clearFrame() = 0;
    virtual void setFrozen(bool on) = 0;
    virtual void setHwPlayback(bool on)
    {
        Q_UNUSED(on);
    }
    virtual void setWebUnderlay(bool on)
    {
        Q_UNUSED(on);
    }
    virtual void setTransitionHold(bool on)
    {
        Q_UNUSED(on);
    }
    virtual void setDrawHud(bool on)
    {
        m_drawHud = on;
        update();
    }
    bool drawHud() const { return m_drawHud; }

    virtual QObject *videoOutput() { return nullptr; }
    virtual QVideoSink *displaySink() const { return nullptr; }

signals:
    void fpsUpdated(int fps);
    void latencyUpdated(int latencyMs);

protected:
    bool m_drawHud = true;

    void reportLatency(qint64 captureMs)
    {
        if (captureMs <= 0)
            return;
        const int ms = int(QDateTime::currentMSecsSinceEpoch() - captureMs);
        if (ms >= 0 && ms < 8000)
            emit latencyUpdated(ms);
    }
};

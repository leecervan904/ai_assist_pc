#pragma once

#include <QObject>
#include <QRect>
#include <QVariantList>
#include <QVariantMap>

class CapturePipeline;
class PreviewSurface;
class QThread;

class HostBridge : public QObject
{
    Q_OBJECT

public:
    HostBridge(CapturePipeline *capture, PreviewSurface *preview, QObject *parent = nullptr);
    ~HostBridge() override;

    Q_INVOKABLE QVariantList devices() const;
    Q_INVOKABLE QVariantMap start(const QString &id, int width, int height, int fps);
    Q_INVOKABLE QVariantMap pickVideoFile();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setInference(bool on);
    Q_INVOKABLE QVariantMap setDetectModel(const QString &id);
    Q_INVOKABLE QString detectModel() const;
    Q_INVOKABLE QVariantList detectModels() const;
    Q_INVOKABLE void inferOnce();
    Q_INVOKABLE void setRoi(bool enabled, int x, int y, int w, int h);
    Q_INVOKABLE void setPreviewRect(double x, double y, double w, double h, bool visible);
    Q_INVOKABLE void setFrozen(bool on);
    Q_INVOKABLE void setCineIndex(int index);
    Q_INVOKABLE void setCinePlaying(bool on, int from, int to);
    Q_INVOKABLE void setCineRingEnabled(bool on);
    Q_INVOKABLE int cineCount() const;
    Q_INVOKABLE QVariantMap cineAi(int index) const;
    Q_INVOKABLE QVariantList frameIds() const;
    Q_INVOKABLE QString frameImageDataUrl(int id) const;
    Q_INVOKABLE QVariantMap frameAi(int id) const;
    Q_INVOKABLE void inferFrame(int id);
    Q_INVOKABLE QVariantMap status() const;
    Q_INVOKABLE QString previewMode() const;
    void setPreviewModeName(const QString &name);
    Q_INVOKABLE void notifyDisplayFps(int fps);
    Q_INVOKABLE void notifyLatency(int latencyMs);
    Q_INVOKABLE void setAiDrawMode(const QString &mode);
    Q_INVOKABLE QString aiDrawMode() const;

signals:
    void aiUpdated(const QVariantMap &ai);
    void roiChanged(int x, int y, int w, int h);
    void previewSize(int w, int h);
    void fpsUpdated(int fps);
    void ratesUpdated(int captureFps, int inferFps);
    void displayFpsUpdated(int fps);
    void latencyUpdated(int latencyMs);
    void cineCountChanged(int count);
    void cineViewChanged(int index);
    void cinePlaybackEnded();
    void frameIdsChanged(const QVariantList &ids);
    void frozenChanged(bool on);
    void captureError(const QString &message);
    void previewPlacement(double x, double y, double w, double h, bool visible);
    void aiDrawModeChanged(const QString &mode);

private:
    CapturePipeline *m_capture = nullptr;
    PreviewSurface *m_preview = nullptr;
    QString m_modeName{QStringLiteral("webchannel")};
    QString m_aiDrawMode{QStringLiteral("web")};
    QString m_lastVideoDir;
};

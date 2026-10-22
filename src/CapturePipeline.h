#pragma once

#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QImage>
#include <QList>
#include <QMediaCaptureSession>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVideoFrame>
#include <QVideoSink>

#include <atomic>

class InferenceEngine;
class QAudioOutput;
class QMediaPlayer;

class FrameWorker : public QObject
{
    Q_OBJECT

public:
    explicit FrameWorker(QObject *parent = nullptr);

public slots:
    void process(const QVideoFrame &frame, bool wantInfer, bool roiOn, QRect roi);
    void snapshotCine(const QVideoFrame &frame, qint64 captureMs);

signals:
    void inferReady(const QImage &image, const QSize &fullSize, const QRect &crop);
    void inferSkip();
    void cineReady(const QImage &image, qint64 captureMs);
};

struct CineFrame {
    QImage image;
    QVariantMap ai;
    int frameId = -1;
    qint64 captureMs = 0;
};

class CapturePipeline : public QObject
{
    Q_OBJECT

public:
    explicit CapturePipeline(QObject *parent = nullptr);
    ~CapturePipeline() override;

    QVariantList listDevices() const;
    QVariantMap startDevice(const QString &id, int width, int height, int fps);
    QVariantMap addVideoFile(const QString &path);
    void stopDevice();
    bool isRunning() const;

    void setDisplay(QObject *output, QVideoSink *sink);
    void setDisplayPaused(bool paused);
    void setInferenceEnabled(bool on);
    bool inferenceEnabled() const { return m_inferenceEnabled.load(); }
    QVariantMap setDetectModel(const QString &id);
    QString detectModel() const;
    QVariantList detectModels() const;
    void setRoi(bool enabled, int x, int y, int w, int h);
    void setFrozen(bool on);
    void inferOnce();
    void setCineIndex(int index);
    void setCinePlaying(bool on, int from, int to);
    void setCineRingEnabled(bool on);
    int cineCount() const;
    QVariantList frameIds() const;
    QImage frameImageById(int id) const;
    QVariantMap frameAiById(int id) const;
    void inferFrameById(int id);
    QImage cineImage(int index) const;
    QVariantMap cineAi(int index) const;
    QVideoFrame latestFrame() const;
    qint64 latestCaptureMs() const;
    QVariantMap latestInference() const;
    QSize latestSize() const;
    bool hasFilePlayer() const { return m_player != nullptr; }
    QString preferredTestVideoId() const;

signals:
    void previewFrame(const QVideoFrame &frame, qint64 captureMs);
    void inferenceResult(const QVariantMap &result);
    void captureError(const QString &message);
    void frameSizeChanged(int width, int height);
    void ratesUpdated(int captureFps, int inferFps);
    void cineCountChanged(int count);
    void cineViewChanged(int index);
    void cinePlaybackEnded();

private slots:
    void onVideoFrame(const QVideoFrame &frame);
    void flushPreview();
    void onInferImage(const QImage &img, const QSize &fullSize, const QRect &crop);
    void onInferenceResult(const QVariantMap &result);
    void onCineReady(const QImage &image, qint64 captureMs);
    void emitRates();
    void onCinePlayTick();
    void onFilePosition(qint64 pos);

private:
    void teardown();
    void bindDisplay();
    void queueInfer(const QVideoFrame &frame, int frameId = -1);
    void queueInferLatest();
    bool queueCineSnapshot(const QVideoFrame &frame, qint64 captureMs);
    QVariantMap startCamera(const QString &id, int width, int height, int fps);
    QVariantMap startFile(const QString &id, const QString &path, int width, int height, int fps);
    static QCameraFormat pickFormat(const QCameraDevice &device, int width, int height, int fps);
    struct VideoFileEntry {
        QString id;
        QString path;
        int width = 0;
        int height = 0;
        int fps = 0;
    };

    QVariantMap videoFileInfo(const VideoFileEntry &entry) const;
    VideoFileEntry *entryForId(const QString &id);
    const VideoFileEntry *entryForId(const QString &id) const;
    void ensureBuiltinVideo();
    void probeVideoFile(VideoFileEntry *entry) const;
    QString pathForFileId(const QString &id) const;
    QString idForVideoPath(const QString &path) const;
    void captureFileCineWindow();
    qint64 filePosForIndex(int index) const;
    int fileIndexForPos(qint64 pos) const;
    int virtualCineCount() const;
    void stopCinePlayback(bool emitEnded);
    void emitFileCineProgress(qint64 pos);

    QMediaCaptureSession *m_session = nullptr;
    QCamera *m_camera = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoSink *m_ownedSink = nullptr;
    QVideoSink *m_activeSink = nullptr;
    QObject *m_displayOutput = nullptr;
    QVideoSink *m_displaySink = nullptr;
    bool m_displayPaused = false;
    bool m_cineRingEnabled = false;

    QThread m_inferThread;
    QThread m_frameThread;
    FrameWorker *m_worker = nullptr;
    InferenceEngine *m_infer = nullptr;
    QString m_detectModel;
    std::atomic<bool> m_inferenceEnabled{false};
    std::atomic<bool> m_inferBusy{false};
    std::atomic<bool> m_alive{false};
    std::atomic<bool> m_previewPosted{false};
    std::atomic<bool> m_frozen{false};
    std::atomic<bool> m_cinePlaying{false};
    std::atomic<bool> m_acceptFrozenFrame{false};
    std::atomic<bool> m_inferDirty{false};
    std::atomic<bool> m_cineBusy{false};
    std::atomic<bool> m_oneShot{false};
    std::atomic<bool> m_acceptFrozenCine{false};
    std::atomic<int> m_capTicks{0};
    std::atomic<int> m_inferTicks{0};

    QTimer m_rateTimer;
    QTimer m_cinePlayTimer;
    qint64 m_lastCineMs = 0;
    qint64 m_fileCineStart = 0;
    qint64 m_fileCineEnd = 0;
    int m_fileCineFrames = 0;
    int m_cinePlayFrom = 0;
    int m_cinePlayTo = 0;
    int m_lastEmittedCineView = -1;

    mutable QMutex m_mutex;
    QVideoFrame m_latestFrame;
    qint64 m_latestCaptureMs = 0;
    QSize m_fullSize;
    QSize m_inferFullSize;
    QRect m_inferCrop;
    QVariantMap m_lastAi;
    bool m_roiEnabled = false;
    QRect m_roi;
    QList<CineFrame> m_cine;
    int m_cineIndex = 0;
    int m_nextCineFrameId = 1;
    int m_inferFrameId = -1;
    QList<VideoFileEntry> m_videoFiles;
};

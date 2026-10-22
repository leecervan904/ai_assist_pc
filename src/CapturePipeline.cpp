#include "CapturePipeline.h"

#include "AssetPaths.h"
#include "InferenceEngine.h"
#include "Log.h"
#include "VideoFrameUtil.h"

#include <QAudioOutput>
#include <QObject>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QMap>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMetaType>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVideoFrameFormat>
#include <algorithm>
#include <limits>

namespace {
constexpr int kCineMax = 360;
constexpr int kCineIntervalMs = 16;
}

namespace {
constexpr char kFileXtId[] = "file:xt";
constexpr char kFileThyroidId[] = "file:thyroid";
constexpr char kFileXt120Id[] = "file:xt120";

QString desktopThyroidVideo()
{
    return QStringLiteral("C:/Users/obliv/Desktop/我的甲状腺.mp4");
}
}

FrameWorker::FrameWorker(QObject *parent)
    : QObject(parent)
{
}

void FrameWorker::process(const QVideoFrame &frame, bool wantInfer, bool roiOn, QRect roi)
{
    if (!wantInfer) {
        emit inferSkip();
        return;
    }
    QVideoFrame clone(frame);
    QImage img = clone.toImage();
    clone = QVideoFrame();
    if (img.isNull()) {
        emit inferSkip();
        return;
    }
    if (img.format() != QImage::Format_RGB888 && img.format() != QImage::Format_RGB32
        && img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_RGB32);

    QImage aiSrc = img;
    QRect crop(0, 0, img.width(), img.height());
    if (roiOn) {
        const QRect sliced = roi.intersected(img.rect());
        if (sliced.width() >= 8 && sliced.height() >= 8) {
            aiSrc = img.copy(sliced);
            crop = sliced;
        }
    }
    emit inferReady(aiSrc, img.size(), crop);
}

void FrameWorker::snapshotCine(const QVideoFrame &frame, qint64 captureMs)
{
    QVideoFrame clone(frame);
    QImage img = clone.toImage();
    clone = QVideoFrame();
    if (img.isNull()) {
        emit cineReady(QImage(), captureMs);
        return;
    }
    emit cineReady(img, captureMs);
}

CapturePipeline::CapturePipeline(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVideoFrame>("QVideoFrame");
    qRegisterMetaType<qint64>("qint64");

    m_worker = new FrameWorker;
    m_worker->moveToThread(&m_frameThread);
    connect(&m_frameThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &FrameWorker::inferReady, this, &CapturePipeline::onInferImage,
            Qt::QueuedConnection);
    connect(m_worker, &FrameWorker::inferSkip, this, [this]() { m_inferBusy = false; });
    connect(m_worker, &FrameWorker::cineReady, this, &CapturePipeline::onCineReady,
            Qt::QueuedConnection);
    m_frameThread.start();

    m_rateTimer.setInterval(1000);
    connect(&m_rateTimer, &QTimer::timeout, this, &CapturePipeline::emitRates);
    m_rateTimer.start();
    m_cinePlayTimer.setTimerType(Qt::PreciseTimer);
    m_cinePlayTimer.setInterval(kCineIntervalMs);
    connect(&m_cinePlayTimer, &QTimer::timeout, this, &CapturePipeline::onCinePlayTick);

    const QVariantMap picked = setDetectModel(QStringLiteral("bee-thyroid"));
    const QString modelPath = picked.value(QStringLiteral("path")).toString();
    m_infer = new InferenceEngine(modelPath.isEmpty() ? findAsset(QStringLiteral("XT-7-640.onnx")) : modelPath);
    m_infer->moveToThread(&m_inferThread);
    connect(&m_inferThread, &QThread::finished, m_infer, &QObject::deleteLater);
    connect(m_infer, &InferenceEngine::resultReady, this, &CapturePipeline::onInferenceResult);
    m_inferThread.start();
    QMetaObject::invokeMethod(m_infer, "initialize", Qt::QueuedConnection);
    ensureBuiltinVideo();
}

CapturePipeline::~CapturePipeline()
{
    stopDevice();
    m_inferThread.quit();
    m_inferThread.wait(2000);
    m_frameThread.quit();
    m_frameThread.wait(2000);
}

QVariantMap CapturePipeline::videoFileInfo(const VideoFileEntry &entry) const
{
    const int w = entry.width > 0 ? entry.width : 1920;
    const int h = entry.height > 0 ? entry.height : 1080;
    const int fps = entry.fps > 0 ? entry.fps : 30;
    QVariantMap mode;
    mode.insert(QStringLiteral("width"), w);
    mode.insert(QStringLiteral("height"), h);
    mode.insert(QStringLiteral("fps"), QVariantList{fps});
    QVariantMap item;
    item.insert(QStringLiteral("id"), entry.id);
    item.insert(QStringLiteral("name"),
                entry.id == QLatin1String(kFileThyroidId)
                    ? QStringLiteral("我的甲状腺")
                    : (entry.id == QLatin1String(kFileXtId)
                           ? QStringLiteral("XT 测试视频")
                           : (entry.id == QLatin1String(kFileXt120Id)
                                  ? QStringLiteral("XT 120fps 测试视频")
                                  : (QStringLiteral("视频 · ")
                                     + QFileInfo(entry.path).fileName()))));
    item.insert(QStringLiteral("modes"), QVariantList{mode});
    item.insert(QStringLiteral("path"), entry.path);
    item.insert(QStringLiteral("kind"), QStringLiteral("file"));
    item.insert(QStringLiteral("locked"), true);
    item.insert(QStringLiteral("width"), w);
    item.insert(QStringLiteral("height"), h);
    item.insert(QStringLiteral("fps"), fps);
    return item;
}

void CapturePipeline::probeVideoFile(VideoFileEntry *entry) const
{
    if (!entry || entry->path.isEmpty())
        return;

    QMediaPlayer player;
    QVideoSink sink;
    QAudioOutput audio;
    audio.setMuted(true);
    player.setAudioOutput(&audio);
    player.setVideoOutput(&sink);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    auto takeMeta = [&]() {
        const QMediaMetaData md = player.metaData();
        const QSize res = md.value(QMediaMetaData::Resolution).toSize();
        if (res.width() >= 16 && res.height() >= 16) {
            entry->width = res.width();
            entry->height = res.height();
        }
        const QVariant rate = md.value(QMediaMetaData::VideoFrameRate);
        bool ok = false;
        const qreal fps = rate.toReal(&ok);
        if (ok && fps > 0.5)
            entry->fps = qMax(1, int(qRound(fps)));
    };

    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged,
                     [&](QMediaPlayer::MediaStatus st) {
                         if (st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia) {
                             takeMeta();
                             if (entry->width >= 16 && entry->fps > 0)
                                 loop.quit();
                         } else if (st == QMediaPlayer::InvalidMedia) {
                             loop.quit();
                         }
                     });
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, [&](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
        if (entry->width < 16 || entry->height < 16) {
            entry->width = frame.width();
            entry->height = frame.height();
        }
        takeMeta();
        loop.quit();
    });

    player.setSource(QUrl::fromLocalFile(entry->path));
    player.play();
    timeout.start(1800);
    loop.exec();
    player.stop();
    if (entry->width < 16 || entry->height < 16) {
        entry->width = 1920;
        entry->height = 1080;
    }
    if (entry->fps <= 0)
        entry->fps = 30;
}

CapturePipeline::VideoFileEntry *CapturePipeline::entryForId(const QString &id)
{
    for (VideoFileEntry &item : m_videoFiles) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

const CapturePipeline::VideoFileEntry *CapturePipeline::entryForId(const QString &id) const
{
    for (const VideoFileEntry &item : m_videoFiles) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

void CapturePipeline::ensureBuiltinVideo()
{
    const QString thyroid = desktopThyroidVideo();
    if (QFileInfo::exists(thyroid) && !entryForId(QString::fromUtf8(kFileThyroidId))) {
        VideoFileEntry entry;
        entry.id = QString::fromUtf8(kFileThyroidId);
        entry.path = QFileInfo(thyroid).absoluteFilePath();
        probeVideoFile(&entry);
        m_videoFiles.push_back(entry);
    }
    const QString path = findAsset(QStringLiteral("XT.mp4"));
    if (QFileInfo::exists(path) && !entryForId(QString::fromUtf8(kFileXtId))) {
        VideoFileEntry entry;
        entry.id = QString::fromUtf8(kFileXtId);
        entry.path = path;
        probeVideoFile(&entry);
        m_videoFiles.push_back(entry);
    }
    const QString path120 = findAsset(QStringLiteral("test-1080p120.mp4"));
    if (QFileInfo::exists(path120) && !entryForId(QString::fromUtf8(kFileXt120Id))) {
        VideoFileEntry entry;
        entry.id = QString::fromUtf8(kFileXt120Id);
        entry.path = path120;
        probeVideoFile(&entry);
        m_videoFiles.push_back(entry);
    }
}

QString CapturePipeline::preferredTestVideoId() const
{
    if (entryForId(QString::fromUtf8(kFileThyroidId)) || QFileInfo::exists(desktopThyroidVideo()))
        return QString::fromUtf8(kFileThyroidId);
    return QString::fromUtf8(kFileXtId);
}

QString CapturePipeline::pathForFileId(const QString &id) const
{
    const VideoFileEntry *entry = entryForId(id);
    return entry ? entry->path : QString();
}

QString CapturePipeline::idForVideoPath(const QString &path) const
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (const VideoFileEntry &item : m_videoFiles) {
        if (QFileInfo(item.path).absoluteFilePath() == abs)
            return item.id;
    }
    return {};
}

QVariantMap CapturePipeline::addVideoFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("找不到视频文件")}};
    }
    const QString abs = info.absoluteFilePath();
    QString id = idForVideoPath(abs);
    if (id.isEmpty()) {
        if (info.fileName().compare(QStringLiteral("我的甲状腺.mp4"), Qt::CaseInsensitive) == 0
            || info.completeBaseName().contains(QStringLiteral("甲状腺")))
            id = QString::fromUtf8(kFileThyroidId);
        else if (info.fileName().compare(QLatin1String("XT.mp4"), Qt::CaseInsensitive) == 0)
            id = QString::fromUtf8(kFileXtId);
        else
            id = QStringLiteral("file:") + QString::fromUtf8(
                     QUrl::toPercentEncoding(QDir::fromNativeSeparators(abs)));
        VideoFileEntry entry;
        entry.id = id;
        entry.path = abs;
        probeVideoFile(&entry);
        m_videoFiles.prepend(entry);
    } else {
        for (int i = 0; i < m_videoFiles.size(); ++i) {
            if (m_videoFiles[i].id == id) {
                if (m_videoFiles[i].width < 16)
                    probeVideoFile(&m_videoFiles[i]);
                m_videoFiles.move(i, 0);
                break;
            }
        }
    }
    const VideoFileEntry *entry = entryForId(id);
    QVariantMap out = entry ? videoFileInfo(*entry)
                            : QVariantMap{{QStringLiteral("ok"), false}};
    out.insert(QStringLiteral("ok"), true);
    return out;
}

QVariantList CapturePipeline::listDevices() const
{
    QVariantList out;
    for (const VideoFileEntry &item : m_videoFiles)
        out.push_back(videoFileInfo(item));

    const auto devices = QMediaDevices::videoInputs();
    for (const QCameraDevice &dev : devices) {
        QMap<QPair<int, int>, QSet<int>> grouped;
        for (const QCameraFormat &fmt : dev.videoFormats()) {
            const QSize res = fmt.resolution();
            if (res.width() < 16 || res.height() < 16)
                continue;
            // Qt 只能 setCameraFormat，会按该档的 maxFrameRate 打开。
            // 不要把区间里的 30/60 插进列表，否则选 60 实际跑的是 120。
            const int hi = std::max(1, int(qRound(fmt.maxFrameRate())));
            grouped[{res.width(), res.height()}].insert(hi);
        }

        QVariantList modes;
        for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
            QList<int> fps = it.value().values();
            std::sort(fps.begin(), fps.end());
            QVariantList fpsVar;
            for (int f : fps)
                fpsVar.push_back(f);
            QVariantMap mode;
            mode.insert(QStringLiteral("width"), it.key().first);
            mode.insert(QStringLiteral("height"), it.key().second);
            mode.insert(QStringLiteral("fps"), fpsVar);
            modes.push_back(mode);
        }
        std::sort(modes.begin(), modes.end(), [](const QVariant &a, const QVariant &b) {
            const auto ma = a.toMap();
            const auto mb = b.toMap();
            const int wa = ma.value(QStringLiteral("width")).toInt();
            const int wb = mb.value(QStringLiteral("width")).toInt();
            if (wa != wb)
                return wa < wb;
            return ma.value(QStringLiteral("height")).toInt() < mb.value(QStringLiteral("height")).toInt();
        });

        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::fromUtf8(dev.id()));
        item.insert(QStringLiteral("name"), dev.description());
        item.insert(QStringLiteral("modes"), modes);
        out.push_back(item);
    }
    return out;
}

QCameraFormat CapturePipeline::pickFormat(const QCameraDevice &device, int width, int height, int fps)
{
    QCameraFormat best;
    int bestScore = std::numeric_limits<int>::min();
    const auto formats = device.videoFormats();
    if (formats.isEmpty())
        return best;

    const float want = float(qMax(1, fps));
    for (const QCameraFormat &fmt : formats) {
        const QSize res = fmt.resolution();
        const float lo = fmt.minFrameRate();
        const float hi = fmt.maxFrameRate();
        int score = 0;
        score -= std::abs(res.width() - width) * 2 + std::abs(res.height() - height) * 2;
        if (res == QSize(width, height))
            score += 8000;
        if (want >= lo - 0.15f && want <= hi + 0.15f)
            score += 2000;
        else if (hi + 0.15f >= want)
            score += 400;
        // 打开时驱动按 maxFrameRate 跑：优先 max 最接近所选值，并惩罚 1–120 这种宽区间。
        score -= int(std::abs(hi - want) * 20);
        score -= int(std::max(0.f, hi - lo) * 2);
        switch (fmt.pixelFormat()) {
        case QVideoFrameFormat::Format_NV12:
        case QVideoFrameFormat::Format_NV21:
        case QVideoFrameFormat::Format_YUYV:
        case QVideoFrameFormat::Format_UYVY:
            score += 400;
            break;
        case QVideoFrameFormat::Format_Jpeg:
            // U4 等采集卡 1K@240 只在 MJPEG 档；低帧率仍避开 JPEG 解码开销。
            score += (want >= 200.f) ? 800 : -800;
            break;
        default:
            break;
        }
        if (score > bestScore) {
            bestScore = score;
            best = fmt;
        }
    }
    return best;
}

QVariantMap CapturePipeline::startDevice(const QString &id, int width, int height, int fps)
{
    stopDevice();
    if (id == QLatin1String(kFileXtId) || id == QLatin1String(kFileThyroidId)
        || id.startsWith(QLatin1String("file:"))) {
        QString path = pathForFileId(id);
        if (path.isEmpty() && id == QLatin1String(kFileThyroidId))
            path = desktopThyroidVideo();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("找不到所选测试视频")}};
        }
        return startFile(id, path, width, height, fps);
    }
    return startCamera(id, width, height, fps);
}

QVariantMap CapturePipeline::startFile(const QString &id, const QString &path, int width, int height, int fps)
{
    VideoFileEntry *entry = entryForId(id);
    if (entry && (entry->width < 16 || entry->fps <= 0))
        probeVideoFile(entry);
    const int useW = (entry && entry->width > 0) ? entry->width : (width > 0 ? width : 1920);
    const int useH = (entry && entry->height > 0) ? entry->height : (height > 0 ? height : 1080);
    const int useFps = (entry && entry->fps > 0) ? entry->fps : (fps > 0 ? fps : 30);

    m_alive = true;
    m_audio = new QAudioOutput(this);
    m_audio->setMuted(true);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audio);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->setLoops(QMediaPlayer::Infinite);
    bindDisplay();
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &text) {
                qWarning() << "player error" << text;
                emit captureError(text);
            });
    connect(m_player, &QMediaPlayer::positionChanged, this, &CapturePipeline::onFilePosition);
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64) {
        if (m_frozen.load() && m_player) {
            captureFileCineWindow();
            emit cineCountChanged(cineCount());
        }
    });
    m_player->play();

    const QSize res(useW, useH);
    {
        QMutexLocker lock(&m_mutex);
        m_fullSize = res;
    }
    const QVariantMap info = entry ? videoFileInfo(*entry) : QVariantMap{};
    SONO_LOG << "file capture started" << path << res << "fps" << useFps;
    setRoi(false, 0, 0, 0, 0);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), info.value(QStringLiteral("name"))},
        {QStringLiteral("width"), res.width()},
        {QStringLiteral("height"), res.height()},
        {QStringLiteral("fps"), useFps},
        {QStringLiteral("path"), path},
        {QStringLiteral("kind"), QStringLiteral("file")},
    };
}

QVariantMap CapturePipeline::startCamera(const QString &id, int width, int height, int fps)
{
    QCameraDevice chosen;
    for (const QCameraDevice &dev : QMediaDevices::videoInputs()) {
        if (QString::fromUtf8(dev.id()) == id) {
            chosen = dev;
            break;
        }
    }
    if (chosen.isNull() && !QMediaDevices::videoInputs().isEmpty())
        chosen = QMediaDevices::videoInputs().constFirst();
    if (chosen.isNull()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("没有可用的采集设备")}};
    }

    m_alive = true;
    m_session = new QMediaCaptureSession(this);
    m_camera = new QCamera(chosen, this);
    m_session->setCamera(m_camera);

    const QCameraFormat fmt = pickFormat(chosen, width, height, fps);
    if (!fmt.isNull())
        m_camera->setCameraFormat(fmt);

    bindDisplay();
    connect(m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &text) {
                qWarning() << "camera error" << text;
                emit captureError(text);
            });
    m_camera->start();

    const QCameraFormat opened = m_camera->cameraFormat().isNull() ? fmt : m_camera->cameraFormat();
    const QSize res = opened.isNull() ? QSize(width, height) : opened.resolution();
    const int actualFps = opened.isNull()
        ? fps
        : qMax(1, int(qRound(opened.maxFrameRate())));
    {
        QMutexLocker lock(&m_mutex);
        m_fullSize = res;
    }
    SONO_LOG << "camera started" << chosen.description() << res
             << "pixel" << (opened.isNull() ? -1 : int(opened.pixelFormat()))
             << "requested" << fps << "opened" << actualFps
             << "range" << (opened.isNull() ? 0 : opened.minFrameRate())
             << (opened.isNull() ? 0 : opened.maxFrameRate());
    setRoi(false, 0, 0, 0, 0);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("id"), QString::fromUtf8(chosen.id())},
        {QStringLiteral("name"), chosen.description()},
        {QStringLiteral("width"), res.width()},
        {QStringLiteral("height"), res.height()},
        {QStringLiteral("fps"), actualFps},
    };
}

void CapturePipeline::stopDevice()
{
    SONO_LOG << "stopDevice";
    teardown();
}

bool CapturePipeline::isRunning() const
{
    if (m_camera && m_camera->isActive())
        return true;
    if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState)
        return true;
    return m_alive.load() && (m_player || m_camera);
}

namespace {
struct DetectChoice {
    QString id;
    QString name;
    QString file;
};

const DetectChoice kDetectChoices[] = {
    {QStringLiteral("bee-thyroid"), QStringLiteral("甲状腺检测 Bee"), QStringLiteral("thyroid.onnx")},
    {QStringLiteral("xt-7"), QStringLiteral("XT-7 检测"), QStringLiteral("XT-7-640.onnx")},
};

const DetectChoice *findDetectChoice(const QString &id)
{
    for (const DetectChoice &item : kDetectChoices) {
        if (item.id.compare(id, Qt::CaseInsensitive) == 0)
            return &item;
        if (item.file.compare(id, Qt::CaseInsensitive) == 0)
            return &item;
    }
    return nullptr;
}
}

QVariantMap CapturePipeline::setDetectModel(const QString &id)
{
    const DetectChoice *choice = findDetectChoice(id);
    const DetectChoice fallback = kDetectChoices[0];
    const DetectChoice &picked = choice ? *choice : fallback;
    QString path = findAsset(picked.file);
    if (!QFileInfo::exists(path) && picked.id != QLatin1String("xt-7"))
        path = findAsset(QStringLiteral("XT-7-640.onnx"));
    const bool ok = QFileInfo::exists(path);
    m_detectModel = ok && path.endsWith(QLatin1String("thyroid.onnx"), Qt::CaseInsensitive)
        ? QStringLiteral("bee-thyroid")
        : QStringLiteral("xt-7");
    if (ok && m_infer)
        QMetaObject::invokeMethod(m_infer, "load", Qt::QueuedConnection, Q_ARG(QString, path));
    QVariantMap out;
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("id"), m_detectModel);
    out.insert(QStringLiteral("path"), path);
    return out;
}

QString CapturePipeline::detectModel() const
{
    return m_detectModel;
}

QVariantList CapturePipeline::detectModels() const
{
    QVariantList out;
    for (const DetectChoice &item : kDetectChoices) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), item.id);
        row.insert(QStringLiteral("name"), item.name);
        row.insert(QStringLiteral("file"), item.file);
        row.insert(QStringLiteral("available"), QFileInfo::exists(findAsset(item.file)));
        out.push_back(row);
    }
    return out;
}

void CapturePipeline::setInferenceEnabled(bool on)
{
    m_inferenceEnabled = on;
    if (!on) {
        m_inferBusy = false;
        m_inferDirty = false;
        QMutexLocker lock(&m_mutex);
        m_lastAi.clear();
        emit inferenceResult(QVariantMap{});
        return;
    }
    if (m_frozen.load())
        queueInferLatest();
}

void CapturePipeline::inferOnce()
{
    m_oneShot = true;
    queueInferLatest();
}

void CapturePipeline::setDisplay(QObject *output, QVideoSink *sink)
{
    m_displayOutput = output;
    m_displaySink = sink;
    if (m_alive.load(std::memory_order_acquire))
        bindDisplay();
}

void CapturePipeline::setDisplayPaused(bool paused)
{
    if (m_displayPaused == paused)
        return;
    m_displayPaused = paused;
    if (m_alive.load(std::memory_order_acquire))
        bindDisplay();
}

void CapturePipeline::bindDisplay()
{
    SONO_LOG << "bindDisplay output" << m_displayOutput << "sink" << m_displaySink
             << "session" << bool(m_session) << "player" << bool(m_player);
    if (m_activeSink)
        disconnect(m_activeSink, nullptr, this, nullptr);
    m_activeSink = nullptr;

    QVideoSink *sink = nullptr;
    if (!m_displayPaused) {
        sink = m_displaySink;
        if (!sink && m_displayOutput)
            sink = m_displayOutput->property("videoSink").value<QVideoSink *>();
    }
    if (!sink) {
        if (!m_ownedSink)
            m_ownedSink = new QVideoSink(this);
        sink = m_ownedSink;
    }
    m_activeSink = sink;
    connect(m_activeSink, &QVideoSink::videoFrameChanged, this, &CapturePipeline::onVideoFrame,
            Qt::DirectConnection);

    if (m_session) {
        if (!m_displayPaused && m_displayOutput) {
            m_session->setVideoSink(nullptr);
            m_session->setVideoOutput(m_displayOutput);
        } else {
            m_session->setVideoOutput(static_cast<QObject *>(nullptr));
            m_session->setVideoSink(sink);
        }
    }
    if (m_player) {
        if (!m_displayPaused && m_displayOutput) {
            m_player->setVideoSink(nullptr);
            m_player->setVideoOutput(m_displayOutput);
        } else {
            m_player->setVideoOutput(static_cast<QObject *>(nullptr));
            m_player->setVideoSink(sink);
        }
    }
}

void CapturePipeline::setRoi(bool enabled, int x, int y, int w, int h)
{
    {
        QMutexLocker lock(&m_mutex);
        m_roiEnabled = enabled;
        m_roi = QRect(x, y, w, h);
    }
    if (m_frozen.load() && m_inferenceEnabled.load())
        queueInferLatest();
}

void CapturePipeline::setFrozen(bool on)
{
    SONO_LOG << "setFrozen" << on << "player" << bool(m_player);
    stopCinePlayback(false);
    m_frozen.store(on, std::memory_order_release);
    if (on && !m_player) {
        QVideoFrame snap;
        qint64 captureMs = 0;
        {
            QMutexLocker lock(&m_mutex);
            snap = m_latestFrame;
            captureMs = m_latestCaptureMs;
        }
        m_acceptFrozenCine.store(true);
        if (snap.isValid())
            queueCineSnapshot(snap, captureMs);
    }
    if (m_player) {
        if (on) {
            m_player->setLoops(1);
            captureFileCineWindow();
            qWarning() << "freeze file cine frames" << m_fileCineFrames
                       << "dur" << m_player->duration() << "count" << cineCount();
            m_player->pause();
        } else if (m_alive.load()) {
            m_player->setLoops(QMediaPlayer::Infinite);
            m_player->play();
        }
    }
    emit cineCountChanged(cineCount());
}

void CapturePipeline::setCineRingEnabled(bool on)
{
    m_cineRingEnabled = on;
}

bool CapturePipeline::queueCineSnapshot(const QVideoFrame &frame, qint64 captureMs)
{
    if (!frame.isValid() || m_cineBusy.exchange(true))
        return false;
    const bool ok = QMetaObject::invokeMethod(m_worker, "snapshotCine", Qt::QueuedConnection,
                                              Q_ARG(QVideoFrame, frame),
                                              Q_ARG(qint64, captureMs));
    if (!ok)
        m_cineBusy = false;
    return ok;
}

void CapturePipeline::onCineReady(const QImage &image, qint64 captureMs)
{
    m_cineBusy = false;
    if (image.isNull()) {
        m_acceptFrozenCine.store(false);
        return;
    }
    if (m_frozen.load() && !m_acceptFrozenCine.exchange(false))
        return;
    if (!m_cineRingEnabled)
        return;
    int n = 0;
    int pushedId = -1;
    QImage pushedImage;
    {
        QMutexLocker lock(&m_mutex);
        CineFrame item;
        item.image = image;
        item.ai = m_lastAi;
        item.frameId = m_nextCineFrameId++;
        item.captureMs = captureMs;
        m_cine.push_back(item);
        while (m_cine.size() > kCineMax)
            m_cine.removeFirst();
        n = m_cine.size();
        m_cineIndex = n - 1;
    }
    emit cineCountChanged(n);
}

int CapturePipeline::virtualCineCount() const
{
    if (m_player && m_frozen.load())
        return qMax(2, m_fileCineFrames);
    return 0;
}

int CapturePipeline::cineCount() const
{
    QMutexLocker lock(&m_mutex);
    return qMax(m_cine.size(), virtualCineCount());
}

QImage CapturePipeline::cineImage(int index) const
{
    QMutexLocker lock(&m_mutex);
    if (m_cine.isEmpty())
        return {};
    const int i = qBound(0, index, m_cine.size() - 1);
    return m_cine.at(i).image;
}

QVariantMap CapturePipeline::cineAi(int index) const
{
    QMutexLocker lock(&m_mutex);
    if (m_cine.isEmpty())
        return {};
    const int i = qBound(0, index, m_cine.size() - 1);
    return m_cine.at(i).ai;
}

QVariantList CapturePipeline::frameIds() const
{
    QMutexLocker lock(&m_mutex);
    QVariantList out;
    for (int i = 0; i < m_cine.size(); ++i) {
        const CineFrame &f = m_cine.at(i);
        QVariantMap item;
        item.insert(QStringLiteral("id"), f.frameId);
        item.insert(QStringLiteral("index"), i);
        item.insert(QStringLiteral("captureMs"), f.captureMs);
        out.append(item);
    }
    return out;
}

QImage CapturePipeline::frameImageById(int id) const
{
    QMutexLocker lock(&m_mutex);
    for (const CineFrame &f : m_cine) {
        if (f.frameId == id)
            return f.image;
    }
    return {};
}

QVariantMap CapturePipeline::frameAiById(int id) const
{
    QMutexLocker lock(&m_mutex);
    for (const CineFrame &f : m_cine) {
        if (f.frameId == id)
            return f.ai;
    }
    return {};
}

void CapturePipeline::inferFrameById(int id)
{
    QVideoFrame frame;
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < m_cine.size(); ++i) {
            if (m_cine.at(i).frameId == id) {
                m_cineIndex = i;
                m_inferFrameId = id;
                frame = videoFrameFromImage(m_cine.at(i).image);
                break;
            }
        }
    }
    if (!frame.isValid())
        return;
    if (m_inferBusy.load()) {
        m_inferDirty.store(true);
        return;
    }
    m_oneShot.store(true);
    queueInfer(frame, id);
}

void CapturePipeline::setCineIndex(int index)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_cine.isEmpty())
            m_cineIndex = qMax(0, index);
        else
            m_cineIndex = qBound(0, index, m_cine.size() - 1);
    }
    if (m_player && m_frozen.load() && !m_cinePlaying.load()) {
        m_acceptFrozenFrame.store(true);
        m_player->setPosition(filePosForIndex(index));
    }
}

void CapturePipeline::captureFileCineWindow()
{
    if (!m_player)
        return;
    const qint64 pos = qMax(qint64(0), m_player->position());
    const qint64 dur = qMax(qint64(0), m_player->duration());
    m_fileCineStart = 0;
    m_fileCineEnd = dur > 0 ? dur : qMax(pos, qint64(2000));
    if (m_fileCineEnd < 400)
        m_fileCineEnd = 2000;
    m_fileCineFrames = qMax(2, int((m_fileCineEnd - m_fileCineStart) * 12 / 1000));
    m_lastEmittedCineView = -1;
}

qint64 CapturePipeline::filePosForIndex(int index) const
{
    const int n = qMax(2, virtualCineCount());
    const int idx = qBound(0, index, n - 1);
    const qint64 span = qMax(qint64(1), m_fileCineEnd - m_fileCineStart);
    return m_fileCineStart + span * idx / (n - 1);
}

int CapturePipeline::fileIndexForPos(qint64 pos) const
{
    const int n = qMax(2, virtualCineCount());
    const qint64 span = qMax(qint64(1), m_fileCineEnd - m_fileCineStart);
    return qBound(0, int((pos - m_fileCineStart) * (n - 1) / span), n - 1);
}

void CapturePipeline::stopCinePlayback(bool emitEnded)
{
    const bool was = m_cinePlaying.exchange(false);
    m_cinePlayTimer.stop();
    if (m_player && m_frozen.load())
        m_player->pause();
    if (emitEnded && was)
        emit cinePlaybackEnded();
}

void CapturePipeline::setCinePlaying(bool on, int from, int to)
{
    SONO_LOG << "setCinePlaying" << on << from << to << "frozen" << m_frozen.load()
             << "file" << bool(m_player);
    if (!m_frozen.load())
        on = false;
    const int n = cineCount();
    m_cinePlayFrom = qBound(0, from, qMax(0, n - 1));
    m_cinePlayTo = to < 0 ? qMax(0, n - 1) : qBound(m_cinePlayFrom, to, qMax(0, n - 1));
    m_lastEmittedCineView = -1;
    if (!on || m_cinePlayTo <= m_cinePlayFrom) {
        stopCinePlayback(false);
        return;
    }
    m_cinePlaying.store(true);
    qWarning() << "cine play" << on << "from" << m_cinePlayFrom << "to" << m_cinePlayTo
               << "count" << n << "file" << bool(m_player);
    if (m_player) {
        m_acceptFrozenFrame.store(true);
        m_player->setLoops(1);
        m_player->setPosition(filePosForIndex(m_cinePlayFrom));
        m_player->play();
        return;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_cineIndex = m_cinePlayFrom;
    }
    emit cineViewChanged(m_cinePlayFrom);
    m_cinePlayTimer.start();
}

void CapturePipeline::onCinePlayTick()
{
    if (!m_frozen.load() || !m_cinePlaying.load() || m_player) {
        m_cinePlayTimer.stop();
        return;
    }
    int emitIdx = -1;
    bool ended = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_cine.size() < 2) {
            ended = true;
        } else {
            const int next = m_cineIndex + 1;
            if (next >= m_cinePlayTo) {
                m_cineIndex = m_cinePlayTo;
                emitIdx = m_cinePlayTo;
                ended = true;
            } else {
                m_cineIndex = next;
                emitIdx = next;
            }
        }
    }
    if (emitIdx >= 0)
        emit cineViewChanged(emitIdx);
    if (ended)
        stopCinePlayback(true);
}

void CapturePipeline::onFilePosition(qint64 pos)
{
    if (!m_player || !m_frozen.load() || !m_cinePlaying.load())
        return;
    emitFileCineProgress(pos);
}

void CapturePipeline::emitFileCineProgress(qint64 pos)
{
    if (pos >= m_fileCineEnd - 30 || fileIndexForPos(pos) >= m_cinePlayTo) {
        const int endIdx = m_cinePlayTo;
        emit cineViewChanged(endIdx);
        stopCinePlayback(true);
        return;
    }
    const int idx = fileIndexForPos(pos);
    if (idx != m_lastEmittedCineView) {
        m_lastEmittedCineView = idx;
        emit cineViewChanged(idx);
    }
}

void CapturePipeline::emitRates()
{
    const int cap = m_capTicks.exchange(0);
    const int infer = m_inferTicks.exchange(0);
    if (m_alive.load() || cap > 0 || infer > 0)
        SONO_LOG << "fps capture" << cap << "infer" << infer
                 << "size" << m_fullSize << "frozen" << m_frozen.load();
    emit ratesUpdated(cap, infer);
}

void CapturePipeline::queueInfer(const QVideoFrame &frame, int frameId)
{
    if ((!m_inferenceEnabled && !m_oneShot) || !m_infer || !frame.isValid())
        return;
    if (m_inferBusy.exchange(true))
        return;
    {
        QMutexLocker lock(&m_mutex);
        m_inferFrameId = frameId;
    }
    bool roiOn = false;
    QRect roi;
    QSize full;
    {
        QMutexLocker lock(&m_mutex);
        roiOn = m_roiEnabled;
        roi = m_roi;
        full = m_fullSize;
    }
    const QSize fs = frame.size();
    if (roiOn && full.width() > 0 && full.height() > 0 && fs.isValid()
        && (fs.width() != full.width() || fs.height() != full.height())) {
        const double sx = double(fs.width()) / double(full.width());
        const double sy = double(fs.height()) / double(full.height());
        roi = QRect(qRound(roi.x() * sx), qRound(roi.y() * sy),
                    qRound(roi.width() * sx), qRound(roi.height() * sy));
    }
    const bool ok = QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                                              Q_ARG(QVideoFrame, frame),
                                              Q_ARG(bool, true),
                                              Q_ARG(bool, roiOn),
                                              Q_ARG(QRect, roi));
    if (!ok)
        m_inferBusy = false;
}

void CapturePipeline::queueInferLatest()
{
    QVideoFrame frame;
    {
        QMutexLocker lock(&m_mutex);
        const bool historical = m_frozen.load() && !m_cine.isEmpty()
            && m_cineIndex >= 0 && m_cineIndex < m_cine.size() - 1;
        if (historical) {
            m_inferFrameId = m_cine.at(m_cineIndex).frameId;
            frame = videoFrameFromImage(m_cine.at(m_cineIndex).image);
        } else {
            m_inferFrameId = -1;
            frame = m_latestFrame;
        }
    }
    if (m_inferBusy.load()) {
        m_inferDirty = true;
        return;
    }
    queueInfer(frame);
}

QVideoFrame CapturePipeline::latestFrame() const
{
    QMutexLocker lock(&m_mutex);
    return m_latestFrame;
}

qint64 CapturePipeline::latestCaptureMs() const
{
    QMutexLocker lock(&m_mutex);
    return m_latestCaptureMs;
}

QVariantMap CapturePipeline::latestInference() const
{
    QMutexLocker lock(&m_mutex);
    return m_lastAi;
}

QSize CapturePipeline::latestSize() const
{
    QMutexLocker lock(&m_mutex);
    return m_fullSize;
}

void CapturePipeline::onVideoFrame(const QVideoFrame &frame)
{
    if (!m_alive.load(std::memory_order_acquire) || !frame.isValid())
        return;
    const bool frozen = m_frozen.load(std::memory_order_acquire);
    const bool replay = frozen && m_cinePlaying.load(std::memory_order_acquire) && m_player;
    const bool seekFrame = frozen && m_acceptFrozenFrame.exchange(false);
    if (frozen && !replay && !seekFrame)
        return;

    const QSize sz = frame.size();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool sizeChanged = false;
    {
        QMutexLocker lock(&m_mutex);
        m_latestFrame = frame;
        m_latestCaptureMs = now;
        if (sz.isValid() && sz != m_fullSize) {
            m_fullSize = sz;
            sizeChanged = true;
        } else if (sz.isValid() && m_fullSize.isEmpty()) {
            m_fullSize = sz;
            sizeChanged = true;
        }
    }
    if (sizeChanged)
        emit frameSizeChanged(sz.width(), sz.height());

    const int ticks = m_capTicks.fetch_add(1) + 1;
    if (ticks == 1)
        SONO_LOG << "first frame" << sz << "pixel" << int(frame.pixelFormat())
                 << "handleType" << int(frame.handleType());
    if (!frozen && now - m_lastCineMs >= kCineIntervalMs) {
        m_lastCineMs = now;
        queueCineSnapshot(frame, now);
    }

    const bool needPreview = frozen || (ticks % 24 == 1);
    if (needPreview && !m_previewPosted.exchange(true))
        QMetaObject::invokeMethod(this, "flushPreview", Qt::QueuedConnection);

    if (!frozen)
        queueInfer(frame);
}

void CapturePipeline::flushPreview()
{
    QVideoFrame frame;
    qint64 captureMs = 0;
    {
        QMutexLocker lock(&m_mutex);
        frame = m_latestFrame;
        captureMs = m_latestCaptureMs;
        m_previewPosted = false;
    }
    if (m_alive.load(std::memory_order_acquire) && frame.isValid())
        emit previewFrame(frame, captureMs);
    if (m_frozen.load() && m_cinePlaying.load() && m_player)
        emitFileCineProgress(m_player->position());
}

void CapturePipeline::onInferImage(const QImage &img, const QSize &fullSize, const QRect &crop)
{
    if (!m_infer) {
        m_inferBusy = false;
        return;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_inferFullSize = fullSize;
        m_inferCrop = crop;
    }
    QMetaObject::invokeMethod(m_infer, "process", Qt::QueuedConnection, Q_ARG(QImage, img));
}

void CapturePipeline::onInferenceResult(const QVariantMap &result)
{
    const bool oneShot = m_oneShot.exchange(false);
    if (!m_inferenceEnabled.load() && !oneShot) {
        m_inferBusy = false;
        m_inferDirty = false;
        return;
    }
    m_inferTicks.fetch_add(1);
    QVariantMap mapped = result;
    {
        QMutexLocker lock(&m_mutex);
        const QSize full = m_inferFullSize.isEmpty() ? m_fullSize : m_inferFullSize;
        QRect crop = m_inferCrop;
        if (crop.width() <= 0 || crop.height() <= 0)
            crop = QRect(QPoint(0, 0), full);
        const double fw = double(qMax(1, full.width()));
        const double fh = double(qMax(1, full.height()));
        const double rx = double(crop.x());
        const double ry = double(crop.y());
        const double rw = double(crop.width());
        const double rh = double(crop.height());
        auto map01 = [&](double v, double origin, double span, double den) {
            return (origin + v * span) / den;
        };
        if (mapped.contains(QStringLiteral("cx"))) {
            mapped.insert(QStringLiteral("cx"),
                          map01(mapped.value(QStringLiteral("cx")).toDouble(), rx, rw, fw));
            mapped.insert(QStringLiteral("cy"),
                          map01(mapped.value(QStringLiteral("cy")).toDouble(), ry, rh, fh));
        }
        QVariantList boxes = mapped.value(QStringLiteral("boxes")).toList();
        QVariantList outBoxes;
        for (const QVariant &item : boxes) {
            QVariantMap b = item.toMap();
            b.insert(QStringLiteral("x1"),
                     map01(b.value(QStringLiteral("x1")).toDouble(), rx, rw, fw));
            b.insert(QStringLiteral("y1"),
                     map01(b.value(QStringLiteral("y1")).toDouble(), ry, rh, fh));
            b.insert(QStringLiteral("x2"),
                     map01(b.value(QStringLiteral("x2")).toDouble(), rx, rw, fw));
            b.insert(QStringLiteral("y2"),
                     map01(b.value(QStringLiteral("y2")).toDouble(), ry, rh, fh));
            outBoxes.push_back(b);
        }
        mapped.insert(QStringLiteral("boxes"), outBoxes);
        if (m_inferFrameId >= 0)
            mapped.insert(QStringLiteral("frameId"), m_inferFrameId);
        m_lastAi = mapped;
        if (!m_cine.isEmpty()) {
            const int i = qBound(0, m_cineIndex, m_cine.size() - 1);
            m_cine[i].ai = mapped;
        }
    }
    m_inferBusy = false;
    {
        const int n = m_inferTicks.load();
        if (n <= 3 || n % 25 == 0)
            SONO_LOG << "infer #" << n << "boxes"
                     << mapped.value(QStringLiteral("boxes")).toList().size()
                     << "label" << mapped.value(QStringLiteral("label")).toString();
    }
    emit inferenceResult(mapped);
    if (m_inferDirty.exchange(false) && m_inferenceEnabled.load())
        queueInferLatest();
}

void CapturePipeline::teardown()
{
    SONO_LOG << "teardown";
    stopCinePlayback(false);
    m_alive.store(false, std::memory_order_release);
    m_fileCineStart = 0;
    m_fileCineEnd = 0;
    m_fileCineFrames = 0;
    if (m_activeSink)
        disconnect(m_activeSink, nullptr, this, nullptr);
    m_activeSink = nullptr;

    if (m_player) {
        m_player->stop();
        m_player->setVideoOutput(static_cast<QObject *>(nullptr));
        m_player->setSource(QUrl());
        delete m_player;
        m_player = nullptr;
    }
    if (m_audio) {
        delete m_audio;
        m_audio = nullptr;
    }
    if (m_session) {
        m_session->setVideoOutput(static_cast<QObject *>(nullptr));
        m_session->setVideoSink(nullptr);
        m_session->setCamera(nullptr);
    }
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_session) {
        delete m_session;
        m_session = nullptr;
    }
    if (m_ownedSink) {
        delete m_ownedSink;
        m_ownedSink = nullptr;
    }

    QMutexLocker lock(&m_mutex);
    m_latestFrame = QVideoFrame();
    m_cine.clear();
    m_previewPosted = false;
    m_inferBusy = false;
    m_cineBusy = false;
    m_frozen = false;
    m_lastCineMs = 0;
}

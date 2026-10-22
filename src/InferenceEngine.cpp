#include "InferenceEngine.h"
#include "Log.h"

#include <QColor>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace {

enum class Layout { YoloV8, ThyroidSeg };

constexpr int kYoloSide = 640;
constexpr int kThyW = 480;
constexpr int kThyH = 640;
constexpr float kYoloConf = 0.25f;
constexpr float kYoloIou = 0.45f;
constexpr int kYoloMax = 300;
constexpr float kThyConf = 0.53f;
constexpr float kThyIou = 0.70f;
constexpr int kThyMax = 15;
constexpr int kThyNc = 6;
constexpr float kMaxWh = 7680.f;
constexpr float kInv255 = 1.f / 255.f;

const char *kYoloNames[] = {
    "enemy", "e_head", "teammate", "xiaobing", "daodi", "bachang", "bachangtou"
};
const char *kThyNames[] = {
    "甲状腺长轴", "颈动脉", "甲状腺", "颈动脉长轴", "喉管", "结节"
};

struct Det {
    float x1, y1, x2, y2, score;
    int cls;
};

float iouOf(const Det &a, const Det &b)
{
    const float x1 = std::max(a.x1, b.x1);
    const float y1 = std::max(a.y1, b.y1);
    const float x2 = std::min(a.x2, b.x2);
    const float y2 = std::min(a.y2, b.y2);
    const float inter = std::max(0.f, x2 - x1) * std::max(0.f, y2 - y1);
    const float aa = std::max(0.f, a.x2 - a.x1) * std::max(0.f, a.y2 - a.y1);
    const float bb = std::max(0.f, b.x2 - b.x1) * std::max(0.f, b.y2 - b.y1);
    return inter / (aa + bb - inter + 1e-6f);
}

std::vector<Det> nmsClassAware(std::vector<Det> dets, float iouThres, int maxDets)
{
    std::sort(dets.begin(), dets.end(), [](const Det &a, const Det &b) { return a.score > b.score; });
    std::vector<Det> keep;
    std::vector<char> dead(dets.size(), 0);
    for (size_t i = 0; i < dets.size(); ++i) {
        if (dead[i])
            continue;
        keep.push_back(dets[i]);
        if (int(keep.size()) >= maxDets)
            break;
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!dead[j] && dets[i].cls == dets[j].cls && iouOf(dets[i], dets[j]) > iouThres)
                dead[j] = 1;
        }
    }
    return keep;
}

std::vector<Det> nmsThyroid(std::vector<Det> dets)
{
    std::sort(dets.begin(), dets.end(), [](const Det &a, const Det &b) { return a.score > b.score; });
    std::vector<Det> keep;
    std::vector<char> dead(dets.size(), 0);
    for (size_t i = 0; i < dets.size(); ++i) {
        if (dead[i])
            continue;
        keep.push_back(dets[i]);
        if (int(keep.size()) >= kThyMax)
            break;
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (dead[j])
                continue;
            const float thresh = dets[i].cls == dets[j].cls ? kThyIou * 0.8f : kThyIou;
            if (iouOf(dets[i], dets[j]) > thresh)
                dead[j] = 1;
        }
    }
    std::array<int, 16> best{};
    best.fill(-1);
    for (int i = 0; i < int(keep.size()); ++i) {
        const int c = keep[size_t(i)].cls;
        if (c < 0 || c >= int(best.size()))
            continue;
        if (best[size_t(c)] < 0 || keep[size_t(i)].score > keep[size_t(best[size_t(c)])].score)
            best[size_t(c)] = i;
    }
    std::vector<Det> out;
    for (int c = 0; c < int(best.size()); ++c) {
        if (best[size_t(c)] >= 0)
            out.push_back(keep[size_t(best[size_t(c)])]);
    }
    std::sort(out.begin(), out.end(), [](const Det &a, const Det &b) { return a.score > b.score; });
    return out;
}

struct Letterbox {
    float gain = 1.f;
    int padX = 0;
    int padY = 0;
    int newW = kYoloSide;
    int newH = kYoloSide;
};

Letterbox letterboxMeta(int srcW, int srcH)
{
    Letterbox lb;
    lb.gain = std::min(float(kYoloSide) / float(srcH), float(kYoloSide) / float(srcW));
    lb.newW = std::max(1, int(std::round(srcW * lb.gain)));
    lb.newH = std::max(1, int(std::round(srcH * lb.gain)));
    lb.padX = int(std::round((float(kYoloSide) - float(srcW) * lb.gain) / 2.f - 0.1f));
    lb.padY = int(std::round((float(kYoloSide) - float(srcH) * lb.gain) / 2.f - 0.1f));
    return lb;
}

void fillLetterboxRgb(const QImage &rgb, const Letterbox &lb, float *chw)
{
    QImage canvas(kYoloSide, kYoloSide, QImage::Format_RGB32);
    canvas.fill(QColor(114, 114, 114));
    const QImage resized = rgb.scaled(lb.newW, lb.newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(lb.padX, lb.padY, resized);
    p.end();

    const int n = kYoloSide * kYoloSide;
    for (int y = 0; y < kYoloSide; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
        for (int x = 0; x < kYoloSide; ++x) {
            const QRgb px = row[x];
            const int idx = y * kYoloSide + x;
            chw[0 * n + idx] = float(qRed(px)) * kInv255;
            chw[1 * n + idx] = float(qGreen(px)) * kInv255;
            chw[2 * n + idx] = float(qBlue(px)) * kInv255;
        }
    }
}

void fillStretchRgb(const QImage &rgb, int dstW, int dstH, float *chw)
{
    const QImage resized = rgb.scaled(dstW, dstH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGB32);
    const int n = dstW * dstH;
    for (int y = 0; y < dstH; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(resized.constScanLine(y));
        for (int x = 0; x < dstW; ++x) {
            const QRgb px = row[x];
            const int idx = y * dstW + x;
            chw[0 * n + idx] = float(qRed(px)) * kInv255;
            chw[1 * n + idx] = float(qGreen(px)) * kInv255;
            chw[2 * n + idx] = float(qBlue(px)) * kInv255;
        }
    }
}

float sigmoid(float x)
{
    return 1.f / (1.f + std::exp(-x));
}

Layout layoutFromPath(const QString &path)
{
    const QString base = QFileInfo(path).completeBaseName().toLower();
    if (base.contains(QLatin1String("thyroid")))
        return Layout::ThyroidSeg;
    return Layout::YoloV8;
}

} // namespace

struct InferenceEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "sono"};
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string inputName;
    std::string outputName;
    std::array<char, 16> clsOn{};
    Layout layout = Layout::YoloV8;
    bool optsReady = false;
};

void loadClassMask(const QString &modelPath, std::array<char, 16> *clsOn)
{
    clsOn->fill(1);
    QFileInfo info(modelPath);
    const QString iniPath = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".ini");
    QFile ini(iniPath);
    if (!ini.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    bool inEnabled = false;
    while (!ini.atEnd()) {
        const QByteArray line = ini.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
            continue;
        if (line.startsWith('[')) {
            inEnabled = QString::fromLatin1(line).compare(QLatin1String("[Enabled]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inEnabled)
            continue;
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        bool ok = false;
        const int id = line.left(eq).trimmed().toInt(&ok);
        if (!ok || id < 0 || id >= int(clsOn->size()))
            continue;
        (*clsOn)[size_t(id)] = line.mid(eq + 1).trimmed() != "0";
    }
}

InferenceEngine::InferenceEngine(QString modelPath, QObject *parent)
    : QObject(parent)
    , m_modelPath(std::move(modelPath))
    , m_impl(std::make_unique<Impl>())
{
    m_impl->clsOn.fill(1);
}

InferenceEngine::~InferenceEngine() = default;

void InferenceEngine::initialize()
{
    if (m_modelPath.isEmpty() || !QFile::exists(m_modelPath)) {
        SONO_WARN << "ONNX model missing:" << m_modelPath;
        m_impl->session.reset();
        return;
    }
    try {
        if (!m_impl->optsReady) {
            m_impl->opts.SetIntraOpNumThreads(2);
            m_impl->opts.SetInterOpNumThreads(1);
            m_impl->opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            m_impl->optsReady = true;
        }
#if defined(_WIN32)
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->env, reinterpret_cast<const wchar_t *>(m_modelPath.utf16()), m_impl->opts);
#else
        const QByteArray modelPathBytes = QFile::encodeName(m_modelPath);
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->env, modelPathBytes.constData(), m_impl->opts);
#endif
        Ort::AllocatorWithDefaultOptions alloc;
        m_impl->inputName = m_impl->session->GetInputNameAllocated(0, alloc).get();
        m_impl->outputName = m_impl->session->GetOutputNameAllocated(0, alloc).get();
        m_impl->layout = layoutFromPath(m_modelPath);
        loadClassMask(m_modelPath, &m_impl->clsOn);
        SONO_LOG << "ONNX loaded" << m_modelPath << m_impl->inputName.c_str()
                 << "->" << m_impl->outputName.c_str()
                 << (m_impl->layout == Layout::ThyroidSeg ? "thyroid-seg" : "yolov8");
    } catch (const Ort::Exception &ex) {
        SONO_ERR << "ONNX init failed:" << ex.what();
        m_impl->session.reset();
    }
}

void InferenceEngine::load(const QString &modelPath)
{
    if (!modelPath.isEmpty())
        m_modelPath = modelPath;
    m_impl->session.reset();
    initialize();
}

void InferenceEngine::process(const QImage &frame)
{
    QElapsedTimer timer;
    timer.start();
    QVariantMap out;
    out.insert(QStringLiteral("backend"), QStringLiteral("onnx"));
    out.insert(QStringLiteral("mask"), true);
    out.insert(QStringLiteral("box"), true);
    out.insert(QStringLiteral("model"), QFileInfo(m_modelPath).fileName());

    auto emitEmpty = [&]() {
        out.insert(QStringLiteral("boxes"), QVariantList{});
        out.insert(QStringLiteral("latencyMs"), int(timer.elapsed()));
        emit resultReady(out);
    };

    if (!m_impl->session || frame.isNull() || frame.width() < 8 || frame.height() < 8) {
        emitEmpty();
        return;
    }

    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    const int srcW = rgb.width();
    const int srcH = rgb.height();
    const bool thyroid = m_impl->layout == Layout::ThyroidSeg;
    const int inW = thyroid ? kThyW : kYoloSide;
    const int inH = thyroid ? kThyH : kYoloSide;
    std::vector<float> input(size_t(3 * inW * inH));
    Letterbox lb;
    if (thyroid)
        fillStretchRgb(rgb, inW, inH, input.data());
    else {
        lb = letterboxMeta(srcW, srcH);
        fillLetterboxRgb(rgb, lb, input.data());
    }

    const std::array<int64_t, 4> shape{1, 3, inH, inW};
    try {
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            m_impl->mem, input.data(), input.size(), shape.data(), shape.size());
        const char *inNames[] = {m_impl->inputName.c_str()};
        const char *outNames[] = {m_impl->outputName.c_str()};
        auto outputs = m_impl->session->Run(Ort::RunOptions{nullptr}, inNames, &tensor, 1, outNames, 1);
        const float *data = outputs[0].GetTensorData<float>();
        const auto dims = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (dims.size() != 3 || dims[1] < 5) {
            emitEmpty();
            return;
        }

        std::vector<Det> dets;
        if (thyroid) {
            const int ch = int(dims[1]);
            const int n = int(dims[2]);
            const int nc = std::min(kThyNc, std::max(0, ch - 4));
            const float sx = float(srcW) / float(inW);
            const float sy = float(srcH) / float(inH);
            const float minArea = float(srcW) * float(srcH) * 0.001f;
            dets.reserve(32);
            for (int i = 0; i < n; ++i) {
                float best = 0.f;
                int cls = 0;
                for (int c = 0; c < nc; ++c) {
                    const float s = sigmoid(data[(4 + c) * n + i]);
                    if (s > best) {
                        best = s;
                        cls = c;
                    }
                }
                if (best < kThyConf)
                    continue;
                if (cls >= 0 && cls < int(m_impl->clsOn.size()) && !m_impl->clsOn[size_t(cls)])
                    continue;
                const float cx = data[0 * n + i];
                const float cy = data[1 * n + i];
                const float bw = data[2 * n + i];
                const float bh = data[3 * n + i];
                if (bw <= 0.f || bh <= 0.f || bw > kMaxWh || bh > kMaxWh)
                    continue;
                Det d;
                d.x1 = (cx - bw * 0.5f) * sx;
                d.y1 = (cy - bh * 0.5f) * sy;
                d.x2 = (cx + bw * 0.5f) * sx;
                d.y2 = (cy + bh * 0.5f) * sy;
                d.score = best;
                d.cls = cls;
                d.x1 = std::clamp(d.x1, 0.f, float(srcW));
                d.y1 = std::clamp(d.y1, 0.f, float(srcH));
                d.x2 = std::clamp(d.x2, 0.f, float(srcW));
                d.y2 = std::clamp(d.y2, 0.f, float(srcH));
                if (d.x2 - d.x1 < 2.f || d.y2 - d.y1 < 2.f)
                    continue;
                if ((d.x2 - d.x1) * (d.y2 - d.y1) < minArea)
                    continue;
                dets.push_back(d);
            }
            dets = nmsThyroid(std::move(dets));
        } else {
            int ch = int(dims[1]);
            int n = int(dims[2]);
            const bool trans = ch > 84 && n >= 5 && n <= 84;
            if (trans)
                std::swap(ch, n);
            const int nc = ch - 4;
            auto at = [&](int c, int i) -> float {
                return trans ? data[i * ch + c] : data[c * n + i];
            };
            dets.reserve(64);
            for (int i = 0; i < n; ++i) {
                float best = 0.f;
                int cls = 0;
                for (int c = 0; c < nc; ++c) {
                    const float s = at(4 + c, i);
                    if (s > best) {
                        best = s;
                        cls = c;
                    }
                }
                if (best < kYoloConf)
                    continue;
                if (cls >= 0 && cls < int(m_impl->clsOn.size()) && !m_impl->clsOn[size_t(cls)])
                    continue;
                const float cx = at(0, i);
                const float cy = at(1, i);
                const float bw = at(2, i);
                const float bh = at(3, i);
                if (bw <= 0.f || bh <= 0.f || bw > kMaxWh || bh > kMaxWh)
                    continue;
                Det d;
                d.x1 = cx - bw * 0.5f;
                d.y1 = cy - bh * 0.5f;
                d.x2 = cx + bw * 0.5f;
                d.y2 = cy + bh * 0.5f;
                d.score = best;
                d.cls = cls;
                dets.push_back(d);
            }
            dets = nmsClassAware(std::move(dets), kYoloIou, kYoloMax);
            const float gain = std::max(lb.gain, 1e-6f);
            for (Det &d : dets) {
                d.x1 = std::clamp((d.x1 - float(lb.padX)) / gain, 0.f, float(srcW));
                d.y1 = std::clamp((d.y1 - float(lb.padY)) / gain, 0.f, float(srcH));
                d.x2 = std::clamp((d.x2 - float(lb.padX)) / gain, 0.f, float(srcW));
                d.y2 = std::clamp((d.y2 - float(lb.padY)) / gain, 0.f, float(srcH));
            }
        }

        QVariantList boxes;
        for (const Det &d : dets) {
            if (d.x2 - d.x1 < 2.f || d.y2 - d.y1 < 2.f)
                continue;
            QVariantMap b;
            b.insert(QStringLiteral("x1"), d.x1 / float(srcW));
            b.insert(QStringLiteral("y1"), d.y1 / float(srcH));
            b.insert(QStringLiteral("x2"), d.x2 / float(srcW));
            b.insert(QStringLiteral("y2"), d.y2 / float(srcH));
            b.insert(QStringLiteral("score"), d.score);
            b.insert(QStringLiteral("cls"), d.cls);
            const char *label = "obj";
            if (thyroid && d.cls >= 0 && d.cls < kThyNc)
                label = kThyNames[d.cls];
            else if (!thyroid && d.cls >= 0 && d.cls < 7)
                label = kYoloNames[d.cls];
            b.insert(QStringLiteral("label"), QString::fromUtf8(label));
            boxes.push_back(b);
        }
        out.insert(QStringLiteral("boxes"), boxes);
        out.insert(QStringLiteral("count"), boxes.size());
        out.insert(QStringLiteral("latencyMs"), int(timer.elapsed()));
        emit resultReady(out);
    } catch (const Ort::Exception &ex) {
        qWarning() << "ONNX run failed:" << ex.what();
        out.insert(QStringLiteral("boxes"), QVariantList{});
        out.insert(QStringLiteral("error"), QString::fromUtf8(ex.what()));
        emit resultReady(out);
    }
}

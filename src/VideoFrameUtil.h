#pragma once

#include <QImage>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QtGlobal>

#include <algorithm>

inline QVideoFrame videoFrameFromImage(const QImage &image)
{
    if (image.isNull())
        return {};

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Qt 6.8 起恢复了 QVideoFrame(const QImage&)，直接使用原生构造路径。
    return QVideoFrame(image);
#else
    // Qt 6.4-6.7 没有图片构造，建一个可写入的 RGBA 帧再逐行拷贝。
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QVideoFrame frame(QVideoFrameFormat(rgba.size(), QVideoFrameFormat::Format_RGBA8888));
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};

    uchar *dst = frame.bits(0);
    if (!dst || frame.planeCount() != 1) {
        frame.unmap();
        return {};
    }

    const int bytesPerLine = qMin(frame.bytesPerLine(0), rgba.bytesPerLine());
    const int lineCount = qMin(frame.height(), rgba.height());
    for (int y = 0; y < lineCount; ++y) {
        std::copy_n(rgba.constScanLine(y), bytesPerLine,
                    dst + y * frame.bytesPerLine(0));
    }
    frame.unmap();
    return frame;
#endif
}

#pragma once

#include "AppMode.h"
#include "CefHost.h"

#include <QMainWindow>
#include <QRect>
#include <QUrl>

class CapturePipeline;
class HostBridge;
class PreviewSurface;
class OverlayWindow;
class QLabel;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(CapturePipeline *capture, const QUrl &pageUrl, AppMode mode,
               QWidget *parent = nullptr);
    void showBenchPreview();
    void evalJs(const QString &js);
    PreviewSurface *previewSurface() const { return m_preview; }
    HostBridge *hostBridge() const { return m_bridge; }

signals:
    void fpsHud(int captureFps, int displayFps, int inferFps, int latencyMs);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void relayoutPreview();
    void onPreviewPlacement(double x, double y, double w, double h, bool visible);
    void refreshHud();
    void setupCefBridge();
    std::string handleSonoQuery(const std::string &request);

    AppMode m_mode = AppMode::WebUnderlay;
    CefRefPtr<CefHost> m_cefHost;
    QWidget *m_underlayHost = nullptr;
    OverlayWindow *m_webOverlay = nullptr;
    PreviewSurface *m_preview = nullptr;
    HostBridge *m_bridge = nullptr;
    QLabel *m_hud = nullptr;
    QUrl m_pageUrl;
    QRect m_cssRect;
    bool m_previewVisible = false;
    bool m_cefStarted = false;
    int m_capFps = 0;
    int m_inferFps = 0;
    int m_displayFps = 0;
    int m_latencyMs = 0;
    int m_srcW = 0;
    int m_srcH = 0;
};

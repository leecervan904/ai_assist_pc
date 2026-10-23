#pragma once

#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_message_router.h"

class CefBridgeHandler;

class CefHost final : public CefClient,
                      public CefRenderHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefRequestHandler
{
public:
    using FrameCallback = std::function<void(const QImage &)>;
    using LoadCallback = std::function<void(bool ok)>;
    using QueryHandler = std::function<std::string(const std::string &)>;

    explicit CefHost(FrameCallback frameCb, LoadCallback loadCb);
    ~CefHost() override;

    void createBrowser(const std::string &url, const QSize &viewSize);
    void closeBrowser();
    void setViewSize(const QSize &size);
    void setDeviceScaleFactor(float scale);

    void sendMouseMove(const QPoint &pos, bool mouseLeave);
    void sendMouseButton(const QPoint &pos, int button, bool down,
                         int clickCount);
    void sendMouseWheel(const QPoint &pos, int deltaX, int deltaY);
    void sendKeyEvent(int windowsKeyCode, int nativeKeyCode, bool down,
                      bool focus, const QString &text);
    void setQueryHandler(QueryHandler handler);
    void pushEvent(const QString &name, const QVariantList &args);

private:
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override;
    bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                       CefScreenInfo &screen_info) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList &dirtyRects, const void *buffer,
                 int width, int height) override;

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode,
                     const CefString &errorText,
                     const CefString &failedUrl) override;

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool user_gesture,
                        bool is_redirect) override;

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status,
                                   int error_code,
                                   const CefString &error_string) override;

    CefRefPtr<CefBrowser> lockedBrowser() const;
    QSize viewSize() const;

    FrameCallback m_frameCb;
    LoadCallback m_loadCb;
    QImage m_image;
    QSize m_size;
    std::atomic<float> m_deviceScaleFactor{1.0f};
    CefRefPtr<CefBrowser> m_browser;
    CefRefPtr<CefMessageRouterBrowserSide> m_router;
    CefBridgeHandler *m_bridgeHandler = nullptr;
    QueryHandler m_queryHandler;
    bool m_paintedLogged = false;
    int m_mouseModifiers = 0;
    mutable std::mutex m_browserMutex;
    mutable std::mutex m_sizeMutex;

    IMPLEMENT_REFCOUNTING(CefHost);
};

bool sonoCefInitialize(const QString &exeDir, int argc, char **argv);
int sonoCefExecuteProcess(int argc, char **argv);
void sonoCefShutdown();
void sonoCefMessageLoopWork();

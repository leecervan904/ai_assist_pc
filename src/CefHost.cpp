#include "CefHost.h"
#include "SonoCefApp.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>

#include "Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include "include/internal/cef_win.h"
#endif

#include "include/cef_task.h"

#ifdef Q_OS_MACOS
#include "include/wrapper/cef_library_loader.h"
#endif

namespace {

#ifdef Q_OS_MACOS
CefScopedLibraryLoader g_macLibraryLoader;
bool g_macLibraryTried = false;
bool g_macLibraryOk = false;

bool sonoCefLoadFramework()
{
    if (!g_macLibraryTried) {
        g_macLibraryTried = true;
        g_macLibraryOk = g_macLibraryLoader.LoadInMain();
    }
    return g_macLibraryOk;
}
#endif

class LambdaTask final : public CefTask
{
public:
    explicit LambdaTask(std::function<void()> fn)
        : m_fn(std::move(fn))
    {
    }

    void Execute() override
    {
        if (m_fn)
            m_fn();
    }

private:
    std::function<void()> m_fn;
    IMPLEMENT_REFCOUNTING(LambdaTask);
};

CefRefPtr<SonoCefApp> g_app;

}  // namespace

class CefBridgeHandler final : public CefMessageRouterBrowserSide::Handler
{
public:
    explicit CefBridgeHandler(CefHost::QueryHandler handler)
        : m_handler(std::move(handler))
    {
    }

    void setHandler(CefHost::QueryHandler handler)
    {
        m_handler = std::move(handler);
    }

    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t query_id,
                 const CefString &request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override
    {
        if (!m_handler)
            return false;
        const std::string response = m_handler(request.ToString());
        callback->Success(response);
        return true;
    }

private:
    CefHost::QueryHandler m_handler;
};

bool sonoCefInitialize(const QString &exeDir, int argc, char **argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    if (g_app)
        return true;

#ifdef Q_OS_MACOS
    if (!sonoCefLoadFramework()) {
        SONO_LOG << "cef framework load failed";
        return false;
    }
#endif

#ifdef Q_OS_WIN
    CefMainArgs args(::GetModuleHandle(nullptr));
#else
    CefMainArgs args(argc, argv);
#endif

    CefSettings settings;
    settings.no_sandbox = true;
#ifdef Q_OS_MACOS
    settings.external_message_pump = true;
#else
    settings.multi_threaded_message_loop = true;
#endif
    settings.windowless_rendering_enabled = true;
    settings.background_color = 0x00000000u;

#ifdef Q_OS_WIN
    if (!exeDir.isEmpty()) {
        const std::wstring dir = exeDir.toStdWString();
        CefString(&settings.resources_dir_path).FromWString(dir);
        CefString(&settings.locales_dir_path)
            .FromWString(dir + L"\\locales");
    }
#elif defined(Q_OS_LINUX)
    if (!exeDir.isEmpty()) {
        const std::string base = exeDir.toStdString();
        CefString(&settings.resources_dir_path).FromString(base);
        CefString(&settings.locales_dir_path)
            .FromString(base + "/locales");
    }
#endif

    g_app = new SonoCefApp();
    return CefInitialize(args, settings, g_app.get(), nullptr);
}

int sonoCefExecuteProcess(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    if (!sonoCefLoadFramework()) {
        SONO_LOG << "cef framework load failed";
        return 1;
    }
#endif
#ifdef Q_OS_WIN
    CefMainArgs args(::GetModuleHandle(nullptr));
#else
    CefMainArgs args(argc, argv);
#endif
    CefRefPtr<SonoCefApp> app = new SonoCefApp();
    return CefExecuteProcess(args, app.get(), nullptr);
}

void sonoCefShutdown()
{
    if (g_app) {
        CefShutdown();
        g_app = nullptr;
    }
}

void sonoCefMessageLoopWork()
{
    CefDoMessageLoopWork();
}

CefHost::CefHost(FrameCallback frameCb, LoadCallback loadCb)
    : m_frameCb(std::move(frameCb))
    , m_loadCb(std::move(loadCb))
{
}

CefHost::~CefHost()
{
    closeBrowser();
}

void CefHost::createBrowser(const std::string &url, const QSize &viewSize)
{
    setViewSize(viewSize);
    SONO_LOG << "cef create browser" << viewSize << "dpr"
             << m_deviceScaleFactor.load(std::memory_order_relaxed);

    CefWindowInfo info;
#ifdef Q_OS_LINUX
    info.SetAsWindowless(0);
#else
    info.SetAsWindowless(nullptr);
#endif

    CefBrowserSettings settings;
    CefBrowserHost::CreateBrowser(info, this, CefString(url), settings,
                                  nullptr, nullptr);
}

void CefHost::closeBrowser()
{
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(m_browserMutex);
        browser = m_browser;
        m_browser = nullptr;
    }
    if (browser)
        browser->GetHost()->CloseBrowser(true);
}

void CefHost::setViewSize(const QSize &size)
{
    {
        std::lock_guard<std::mutex> lock(m_sizeMutex);
        if (m_size == size)
            return;
        m_size = size;
    }
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;
    auto task = new LambdaTask([browser]() {
        browser->GetHost()->WasResized();
    });
    CefPostTask(TID_UI, task);
}

void CefHost::setDeviceScaleFactor(float scale)
{
    if (!std::isfinite(scale) || scale <= 0.0f)
        return;
    scale = std::clamp(scale, 1.0f, 4.0f);
    const float previous =
        m_deviceScaleFactor.exchange(scale, std::memory_order_acq_rel);
    if (std::fabs(previous - scale) < 0.001f)
        return;
    SONO_LOG << "cef device scale" << previous << "->" << scale;
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;
    auto task = new LambdaTask([browser]() {
        browser->GetHost()->NotifyScreenInfoChanged();
        browser->GetHost()->WasResized();
    });
    CefPostTask(TID_UI, task);
}

void CefHost::sendMouseMove(const QPoint &pos, bool mouseLeave)
{
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;
    CefMouseEvent ev;
    ev.x = pos.x();
    ev.y = pos.y();
    ev.modifiers = m_mouseModifiers;
    auto task = new LambdaTask([browser, ev, mouseLeave]() {
        browser->GetHost()->SendMouseMoveEvent(ev, mouseLeave);
    });
    CefPostTask(TID_UI, task);
}

void CefHost::sendMouseButton(const QPoint &pos, int button, bool down,
                              int clickCount)
{
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;
    CefMouseEvent ev;
    ev.x = pos.x();
    ev.y = pos.y();
    auto type = CefBrowserHost::MouseButtonType::MBT_LEFT;
    int buttonModifier = EVENTFLAG_LEFT_MOUSE_BUTTON;
    if (button == static_cast<int>(Qt::RightButton)) {
        type = CefBrowserHost::MouseButtonType::MBT_RIGHT;
        buttonModifier = EVENTFLAG_RIGHT_MOUSE_BUTTON;
    } else if (button == static_cast<int>(Qt::MiddleButton)) {
        type = CefBrowserHost::MouseButtonType::MBT_MIDDLE;
        buttonModifier = EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }
    if (down)
        m_mouseModifiers |= buttonModifier;
    else
        m_mouseModifiers &= ~buttonModifier;
    ev.modifiers = m_mouseModifiers | buttonModifier;
    const bool mouseUp = !down;
    auto task = new LambdaTask([browser, ev, type, mouseUp, clickCount]() {
        browser->GetHost()->SendMouseClickEvent(ev, type, mouseUp, clickCount);
    });
    CefPostTask(TID_UI, task);
}

void CefHost::sendMouseWheel(const QPoint &pos, int deltaX, int deltaY)
{
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;
    CefMouseEvent ev;
    ev.x = pos.x();
    ev.y = pos.y();
    auto task = new LambdaTask([browser, ev, deltaX, deltaY]() {
        browser->GetHost()->SendMouseWheelEvent(ev, deltaX, deltaY);
    });
    CefPostTask(TID_UI, task);
}

void CefHost::sendKeyEvent(int windowsKeyCode, int nativeKeyCode, bool down,
                           bool focus, const QString &text)
{
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;
    CefKeyEvent ev;
    ev.windows_key_code = windowsKeyCode;
    ev.native_key_code = nativeKeyCode;
    ev.focus_on_editable_field = focus;
    if (!text.isEmpty()) {
        ev.type = KEYEVENT_CHAR;
        const ushort *u = text.utf16();
        if (u)
            ev.character = *u;
    } else {
        ev.type = down ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
    }
    auto task = new LambdaTask([browser, ev]() {
        browser->GetHost()->SendKeyEvent(ev);
    });
    CefPostTask(TID_UI, task);
}

void CefHost::setQueryHandler(QueryHandler handler)
{
    m_queryHandler = std::move(handler);
    if (m_bridgeHandler)
        m_bridgeHandler->setHandler(m_queryHandler);
}

CefRefPtr<CefBrowser> CefHost::lockedBrowser() const
{
    std::lock_guard<std::mutex> lock(m_browserMutex);
    return m_browser;
}

QSize CefHost::viewSize() const
{
    std::lock_guard<std::mutex> lock(m_sizeMutex);
    return m_size;
}

void CefHost::pushEvent(const QString &name, const QVariantList &args)
{
    const CefRefPtr<CefBrowser> browser = lockedBrowser();
    if (!browser)
        return;

    const QByteArray payload = QJsonDocument(QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("args"), QJsonArray::fromVariantList(args)}})
                                   .toJson(QJsonDocument::Compact);
    const QString code = QStringLiteral("window.__sonoPush(%1);")
                             .arg(QString::fromUtf8(payload));
    const CefString script = code.toStdString();
    auto task = new LambdaTask([browser, script]() {
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        if (frame)
            frame->ExecuteJavaScript(script, CefString("sono-bridge"), 0);
    });
    CefPostTask(TID_UI, task);
}

void CefHost::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect)
{
    Q_UNUSED(browser);
    const QSize size = viewSize();
    rect.x = 0;
    rect.y = 0;
    rect.width = size.width();
    rect.height = size.height();
}

bool CefHost::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                            CefScreenInfo &screen_info)
{
    Q_UNUSED(browser);
    const QSize size = viewSize();
    const float scale =
        m_deviceScaleFactor.load(std::memory_order_relaxed);
    screen_info.device_scale_factor = scale;
    screen_info.depth = 32;
    screen_info.depth_per_component = 8;
    screen_info.is_monochrome = false;
    screen_info.rect = CefRect(0, 0, size.width(), size.height());
    screen_info.available_rect = screen_info.rect;
    return true;
}

void CefHost::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                      const RectList &dirtyRects, const void *buffer,
                      int width, int height)
{
    if (type != PET_VIEW || !buffer || width <= 0 || height <= 0)
        return;

    const float scale =
        m_deviceScaleFactor.load(std::memory_order_relaxed);
    if (!m_paintedLogged) {
        m_paintedLogged = true;
        SONO_LOG << "cef first paint" << width << "x" << height << "dpr"
                 << scale;
    }

    if (m_image.size() != QSize(width, height)
        || m_image.format() != QImage::Format_ARGB32_Premultiplied) {
        SONO_LOG << "cef paint size" << width << "x" << height << "dpr"
                 << scale;
        m_image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
    }
    m_image.setDevicePixelRatio(scale);

    const uchar *src = static_cast<const uchar *>(buffer);
    const int stride = width * 4;
    for (int y = 0; y < height; ++y)
        memcpy(m_image.scanLine(y), src + y * stride, stride);

    if (m_frameCb)
        m_frameCb(m_image);
}

void CefHost::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
    {
        std::lock_guard<std::mutex> lock(m_browserMutex);
        m_browser = browser;
    }
    browser->GetHost()->SetFocus(true);
    if (!m_router) {
        CefMessageRouterConfig config;
        m_router = CefMessageRouterBrowserSide::Create(config);
    }
    if (!m_bridgeHandler) {
        m_bridgeHandler = new CefBridgeHandler(m_queryHandler);
    }
    if (m_router && m_bridgeHandler) {
        m_router->AddHandler(m_bridgeHandler, false);
    }
    browser->GetHost()->NotifyScreenInfoChanged();
    browser->GetHost()->WasResized();
}

bool CefHost::DoClose(CefRefPtr<CefBrowser> browser)
{
    return false;
}

void CefHost::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
    {
        std::lock_guard<std::mutex> lock(m_browserMutex);
        if (m_browser && m_browser->IsSame(browser))
            m_browser = nullptr;
    }
    if (m_router)
        m_router->OnBeforeClose(browser);
    if (m_router && m_bridgeHandler) {
        m_router->RemoveHandler(m_bridgeHandler);
        delete m_bridgeHandler;
        m_bridgeHandler = nullptr;
    }
}

void CefHost::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int httpStatusCode)
{
    if (frame && frame->IsMain()) {
        static const char kUnderlayCss[] = R"JS(
(function () {
  document.documentElement.classList.add('web-underlay');
  const transparent = (el) => {
    if (el) el.style.setProperty('background', 'transparent', 'important');
  };
  const root = document.documentElement;
  transparent(root);
  transparent(document.body);
  transparent(root.querySelector('.window'));
  transparent(root.querySelector('.workspace'));
  transparent(root.querySelector('.center'));
  transparent(root.querySelector('.vp'));
  transparent(root.querySelector('#scanVp'));
  transparent(root.querySelector('#scanCanvas'));
  const style = document.createElement('style');
  style.textContent = `
html.web-underlay body,
html.web-underlay .window,
html.web-underlay .workspace,
html.web-underlay .center,
html.web-underlay .vp,
html.web-underlay #scanVp,
html.web-underlay #scanCanvas {
  background: transparent !important;
}
`;
  document.head.appendChild(style);
})();
)JS";
        frame->ExecuteJavaScript(CefString(kUnderlayCss),
                                 CefString("sono-underlay"), 1);
    }
    if (m_loadCb && frame && frame->IsMain())
        m_loadCb(httpStatusCode >= 200 && httpStatusCode < 400);
}

void CefHost::OnLoadError(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          ErrorCode errorCode,
                          const CefString &errorText,
                          const CefString &failedUrl)
{
    if (m_loadCb && frame && frame->IsMain())
        m_loadCb(false);
}

bool CefHost::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message)
{
    return m_router
           && m_router->OnProcessMessageReceived(browser, frame, source_process,
                                                 message);
}

bool CefHost::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             CefRefPtr<CefRequest> request,
                             bool user_gesture,
                             bool is_redirect)
{
    if (m_router)
        m_router->OnBeforeBrowse(browser, frame);
    return false;
}

void CefHost::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                        TerminationStatus status,
                                        int error_code,
                                        const CefString &error_string)
{
    if (m_router)
        m_router->OnRenderProcessTerminated(browser);
}

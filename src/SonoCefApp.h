#pragma once

#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_message_router.h"

#include <cstdio>

// Shared CEF app for both the browser process and the macOS helper processes.
// The renderer side installs the sono bridge shim and the CEF message router
// so the off-screen page can talk to the native host via cefQuery.
class SonoCefApp final : public CefApp,
                         public CefBrowserProcessHandler,
                         public CefRenderProcessHandler
{
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
    {
        return this;
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
    {
        return this;
    }

    void OnWebKitInitialized() override
    {
        CefMessageRouterConfig config;
        m_rendererRouter = CefMessageRouterRendererSide::Create(config);
    }

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override
    {
        if (m_rendererRouter)
            m_rendererRouter->OnContextCreated(browser, frame, context);
        if (!frame->IsMain())
            return;

        static const char kSonoShim[] = R"JS(
(function () {
  if (window.__sonoShimInstalled) return;
  window.__sonoShimInstalled = true;
  window.SONO_MODE = "webunderlay";
  window.SONO_CEF = true;
  window.__sonoSignals = {};
  const SIGNALS = [
    "aiUpdated", "roiChanged", "previewSize", "fpsUpdated",
    "ratesUpdated", "displayFpsUpdated", "latencyUpdated",
    "cineCountChanged", "cineViewChanged", "cinePlaybackEnded",
    "frameIdsChanged", "frozenChanged", "captureError", "previewPlacement",
    "aiDrawModeChanged"
  ];
  const METHODS = [
    "devices", "start", "pickVideoFile", "stop", "setInference",
    "setDetectModel", "detectModel", "detectModels", "inferOnce",
    "setRoi", "setPreviewRect", "setFrozen", "setCineIndex",
    "setCinePlaying", "setCineRingEnabled", "cineCount", "cineAi", "frameIds", "status",
    "previewMode", "notifyDisplayFps", "notifyLatency",
    "setAiDrawMode", "aiDrawMode"
  ];
  function Signal(name) {
    this.name = name;
    this.cbs = [];
    this.connect = function (fn) {
      if (typeof fn === "function" && this.cbs.indexOf(fn) < 0) this.cbs.push(fn);
    };
    this.disconnect = function (fn) {
      const i = this.cbs.indexOf(fn);
      if (i >= 0) this.cbs.splice(i, 1);
    };
  }
  SIGNALS.forEach(function (n) { window.__sonoSignals[n] = new Signal(n); });
  function callNative(method, args) {
    return new Promise(function (resolve, reject) {
      if (typeof window.cefQuery !== "function") {
        reject(new Error("cefQuery 未就绪"));
        return;
      }
      window.cefQuery({
        request: JSON.stringify({ method: method, args: args || [] }),
        persistent: false,
        onSuccess: function (response) {
          let payload = null;
          try {
            payload = JSON.parse(response || "null");
          } catch (e) {
            reject(e);
            return;
          }
          if (payload && payload.ok === false) {
            reject(new Error(payload.error || "bridge error"));
            return;
          }
          resolve(payload && Object.prototype.hasOwnProperty.call(payload, "result")
            ? payload.result
            : undefined);
        },
        onFailure: function (code, msg) {
          reject(new Error(String(msg || code)));
        }
      });
    });
  }
  const sono = {};
  sono._isSonoHost = true;
  METHODS.forEach(function (name) {
    sono[name] = function () {
      const args = Array.prototype.slice.call(arguments);
      const cb = typeof args[args.length - 1] === "function" ? args.pop() : null;
      const promise = callNative(name, args);
      if (cb) {
        promise.then(function (r) {
          try { cb(r); } catch (e) { console.error("sono callback", name, e); }
        }, function (e) {
          try { cb(null); } catch (_) {}
        });
      }
      return promise;
    };
  });
  SIGNALS.forEach(function (n) { sono[n] = window.__sonoSignals[n]; });
  window.sono = sono;
  window.__sonoPush = function (payload) {
    if (!payload) return;
    const sig = window.__sonoSignals[payload.name];
    if (!sig) return;
    const list = sig.cbs.slice();
    for (let i = 0; i < list.length; ++i) {
      const args = payload.args || [];
      try { list[i].apply(null, args); }
      catch (e) { console.error("sono signal", payload.name, e); }
    }
  };
})();
)JS";
        CefRefPtr<CefV8Exception> exception;
        CefRefPtr<CefV8Value> retval;
        if (!context->Eval(kSonoShim, CefString("sono-bridge"), 1, retval,
                           exception) && exception) {
            std::fprintf(stderr, "sono shim eval failed: %s\n",
                         exception->GetMessage().ToString().c_str());
        }
    }

    void OnContextReleased(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override
    {
        if (m_rendererRouter)
            m_rendererRouter->OnContextReleased(browser, frame, context);
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override
    {
        return m_rendererRouter
               && m_rendererRouter->OnProcessMessageReceived(
                   browser, frame, source_process, message);
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> m_rendererRouter;
    IMPLEMENT_REFCOUNTING(SonoCefApp);
};

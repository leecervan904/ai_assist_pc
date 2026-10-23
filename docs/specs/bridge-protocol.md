# C++ / Web 桥接通信协议契约 (`bridge-protocol.md`)

本文档定义 C++ 宿主程序（`MainWindow` + `HostBridge` + `CefHost`）与离屏渲染网页（`web/workstation.html`）之间的双向通信协议。

---

## 1. 通信通道概览

| 方向 | 调用发起方 | 传输机制 | C++ 处理器 | Web 端接口 |
| :--- | :--- | :--- | :--- | :--- |
| **Web -> C++ (Request/Response)** | `web/workstation.html` | `window.cefQuery({ request: JSON.stringify(...) })` | `MainWindow::handleSonoQuery` -> `HostBridge` | `window.sono.*` Promise 封装 |
| **C++ -> Web (Event Push)** | `HostBridge` 信号 | `CefFrame::ExecuteJavaScript` | `CefHost::pushEvent(name, args)` | `window.__sonoPush({ name, args })` |

---

## 2. 上行查询协议 (Web -> C++)

请求负载为 JSON 字符串。参数使用数组，避免 Web/C++ 两侧对对象字段名产生重复映射：

```json
{
  "method": "setDetectModel",
  "args": ["bee-thyroid"]
}
```

`window.sono.<method>(...args)` 会在渲染进程中完成封装，通过 `cefQuery` 发送到 `MainWindow::handleSonoQuery()`。

### 响应格式

成功：

```json
{ "ok": true, "result": null }
```

失败：

```json
{ "ok": false, "error": "错误描述" }
```

### 核心方法列表 (`method`)

- `devices`：枚举采集设备与内置测试视频源。
- `start` / `stop`：启动或停止采集，参数为 `id, width, height, fps`。
- `pickVideoFile`：打开本地视频文件选择流程。
- `setInference` / `inferOnce`：开关实时推理或触发单次推理。
- `setDetectModel` / `detectModel` / `detectModels`：切换、查询当前模型或列出模型。
- `setRoi`：参数为 `enabled, x, y, w, h`。
- `setPreviewRect`：参数为 `x, y, w, h, visible`，由页面通知原生视频层的位置。
- `setFrozen`：冻结或恢复实时画面。
- `setCineIndex` / `setCinePlaying` / `setCineRingEnabled`：控制电影回放。
- `cineCount` / `cineAi` / `frameIds`：查询 Cine 缓存和帧信息。
- `status` / `previewMode`：查询宿主状态与预览模式。
- `setAiDrawMode` / `aiDrawMode`：设置或查询 AI 标注绘制归属。
- `notifyDisplayFps` / `notifyLatency`：Web 渲染侧回报显示帧率和延迟。

---

## 3. 下行事件推送协议 (C++ -> Web)

C++ 在 `CefHost::pushEvent()` 中把事件序列化为：

```json
{ "name": "ratesUpdated", "args": [60, 12] }
```

页面中的 `window.__sonoPush(payload)` 再按 `name` 分发到同名 Signal 回调。当前主动推送的主要事件为：

- `aiUpdated`：当前帧 AI 检测框、分割轮廓与置信度。
- `previewSize` / `ratesUpdated` / `displayFpsUpdated` / `latencyUpdated`：尺寸与性能指标。
- `cineCountChanged` / `frameIdsChanged` / `cineViewChanged` / `cinePlaybackEnded`：Cine 状态。
- `roiChanged` / `captureError`：ROI 变化和采集错误。

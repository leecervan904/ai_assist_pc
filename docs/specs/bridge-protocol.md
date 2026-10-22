# C++ / Web 桥接通信协议契约 (`bridge-protocol.md`)

本文档定义 C++ 宿主程序（`MainWindow` + `HostBridge` + `CefHost`）与离屏渲染网页（`workstation.html`）之间的双向通信协议。

---

## 1. 通信通道概览

| 方向 | 调用发起方 | 传输机制 | C++ 处理器 | Web 端接口 |
| :--- | :--- | :--- | :--- | :--- |
| **Web -> C++ (Request/Response)** | `workstation.html` | `window.cefQuery({ request: JSON.stringify(...) })` | `MainWindow::handleSonoQuery` -> `HostBridge` | `window.sono.*` Promise 封装 |
| **C++ -> Web (Event Push)** | `HostBridge` 信号 | `CefFrame::ExecuteJavaScript` | `CefHost::pushEvent(name, payload)` | `window.__sonoPush(event, data)` |

---

## 2. 上行查询协议 (Web -> C++)

请求负载为 JSON 字符串：
```json
{
  "cmd": "setDetectModel",
  "args": {
    "modelId": "bee-thyroid"
  }
}
```

### 核心命令列表 (`cmd`)

- `enumerateSources`：枚举可用视频采集设备与内置测试视频源（如 `file:thyroid`）。
- `start` / `stop`：启动或停止指定视频源采集（参数含 `sourceId`, `width`, `height`, `fps`）。
- `setFrozen` / `seekFrame`：控制超声画面冻结（Freeze）与电影回放（Cine Loop）帧跳转。
- `setRoi`：设置实时 AI 推理的感兴趣区域（ROI 归一化或像素坐标）。
- `setDetectModel`：动态切换 ONNX 检测/分割模型（如 `bee-thyroid` / `xt-7-640`）。

---

## 3. 下行事件推送协议 (C++ -> Web)

C++ 通过调用 `window.__sonoPush(eventName, payloadObject)` 实时向前端推送状态：

- `detections`：当前帧 AI 推理检测框、分割轮廓与置信度数组。
- `stats`：实时性能指标（`captureFps`, `displayFps`, `inferFps`, `inferMs`）。
- `state`：采集状态、冻结状态、当前帧索引与总帧数变更通知。

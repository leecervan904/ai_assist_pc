# 超声智能工作站系统架构 (`overview.md`)

## 1. 系统概述

本项目采用 **原生视频显示 + CEF 离屏网页 UI** 的混合架构：

- 视频流始终由原生 `QVideoWidget` 显示，不走网页回显，保证高帧率（60~240 FPS）低延迟。
- 网页（`web/workstation.html`）运行在 CEF 离屏渲染（Off-Screen Rendering）中，作为上层透明交互/UI 界面。
- 网页负责 ROI、测量、AI 框、交互框等绘制与操作。
- C++ 负责采集、ONNX 实时推理、原生视频显示、C++/JS 桥接。

---

## 2. 总体架构

```text
超声采集设备 / 视频文件 (assets/media/*.mp4)
        │
        ▼
CapturePipeline（采集、ONNX 推理 [assets/models/*.onnx]、cine 回放）
        │
        ▼
PreviewWidget（QVideoWidget，原生视频渲染层）

web/workstation.html（网页 UI / 测量 / ROI / AI 框）
        ▲
        │ window.sono / cefQuery / window.__sonoPush
CefHost（CEF 离屏渲染，OverlayWindow 透明浮动层显示）
        ▲
        │ Qt 信号 / 查询分发
MainWindow + HostBridge
```

---

## 3. 核心链路

### 3.1 视频链路

1. `CapturePipeline` 从采集卡或视频文件接收 `QVideoFrame`。
2. 帧送入 `PreviewWidget` 中的 `QVideoWidget` 原生显示。
3. 网页不参与视频回显，只负责在 CEF 浮动层上绘制 UI 与标注叠加层。

### 3.2 WebUnderlay 模式

- 系统唯一标准运行模式。
- CEF 以离屏渲染方式加载 `web/workstation.html`，构建时复制到可执行文件目录的 `web/` 子目录。
- CEF 输出的每一帧 BGRA 位图通过 `CefHost::OnPaint` 推到 `OverlayWindow` 显示。
- 鼠标与键盘事件由 `OverlayWindow` 接收并转发给 CEF 页面。
- 网页内通过 `cefQuery` 调用 C++ 方法，C++ 通过 `window.__sonoPush` 推送异步状态与 AI 推理结果给网页。

### 3.3 C++ / JS 桥接

- **上行调用**：`web/workstation.html` -> `window.sono.*` -> `cefQuery` -> `MainWindow::handleSonoQuery` -> `HostBridge`。
- **下行推送**：`HostBridge` 信号 -> `MainWindow::setupCefBridge` -> `CefHost::pushEvent` -> 网页 `window.__sonoPush`。

---

## 4. 模块职责说明

| 模块 | 源码位置 | 核心职责 |
| :--- | :--- | :--- |
| `CapturePipeline` | `src/CapturePipeline.*` | 采集卡/视频文件输入、帧调度、推理触发、cine 缓存、ROI 裁剪 |
| `InferenceEngine` | `src/InferenceEngine.*` | ONNX Runtime 会话管理、图像预处理、模型推理（YOLOv8 / 甲状腺分割）、NMS 后处理 |
| `PreviewWidget` | `src/PreviewWidget.*` | 原生 `QVideoWidget` 视频显示容器 |
| `CefHost` | `src/CefHost.*` | CEF 生命周期初始化、离屏渲染回调、输入事件转发 |
| `OverlayWindow` | `src/MainWindow.cpp` | 承载 CEF 离屏位图的透明浮动窗口 |
| `HostBridge` | `src/HostBridge.*` | C++ 业务 API 封装与状态信号发射 |
| `MainWindow` | `src/MainWindow.*` | 主窗口布局管理、C++/JS 查询路由分发 |
| Web Overlay | `web/workstation.html` | 网页 UI、测量工具、ROI、AI 检测框、交互控制面板 |

---

## 5. 跨平台依赖与进程模型

| 平台 | CEF 路径 (`CEF_ROOT`) | ONNX Runtime (`ORT_ROOT`) |
| :--- | :--- | :--- |
| **Windows** | `.deps/cef` | `.deps/onnxruntime` |
| **Linux** | `.deps/cef` | `.deps/onnxruntime` |
| **macOS** | `.deps/cef` | 系统/Homebrew 的 `find_package(onnxruntime CONFIG)` |

> Qt 6.8.2+ 由用户主机手动安装，并由 CMake 负责发现。CEF、ORT 与 ONNX 模型的下载声明由 `deps/manifest.json` 管理：Windows/Linux 从官方预编译归档拉取，macOS ORT 使用系统 CMake package，模型从手动配置的 GitHub Release 下载，统一入口为 `mise run deps:sync`。项目不上传或发布 `.onnx` 文件。

完整的构建产物布局、进程初始化和桥接就绪顺序见 [构建产物与启动链路](../guides/startup-process.md)。

### 5.1 CEF 进程模型

- **Windows / Linux**：单可执行文件结构，通过 `CefExecuteProcess` 分发渲染与 GPU 子进程。
- **macOS**：遵循 Chromium macOS 规范，构建时自动编译独立的 `SonoWorkstation Helper.app` 簇并拷贝进 `.app/Contents/Frameworks/`，执行 ad-hoc 签名。

### 5.2 Linux X11 窗口管理器兼容

Linux 下若 overlay 窗口在轻量级 WM 或虚拟机中被当作独立窗口，可开启编译选项：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSONO_LINUX_X11_BYPASS_WM=ON
```

# 构建产物与启动链路 (`startup-process.md`)

本文描述从 `mise run run` 到 Qt 窗口、CEF 页面、采集线程和桥接协议就绪的完整过程。

## 1. 启动前的运行时布局

CMake 会把 Web UI、模型和测试视频复制到可执行文件目录，同时在 Windows/Linux 部署 CEF/ORT 运行库，在 macOS 部署 CEF framework 和 Helper.app。

### Windows / Linux

```text
build/
├── SonoWorkstation[.exe]
├── web/workstation.html
├── XT-7-640.onnx
├── XT-7-640.ini
├── thyroid.onnx
├── thyroid.ini
├── XT.mp4
├── test-1080p120.mp4
├── CEF runtime / resources
└── onnxruntime runtime
```

Windows Visual Studio 生成器通常把可执行文件放在 `build/Debug/` 或 `build/Release/`；Ninja 生成器放在 `build/`。

### macOS

```text
build/SonoWorkstation.app/
└── Contents/
    ├── MacOS/
    │   ├── SonoWorkstation
    │   ├── web/workstation.html
    │   ├── ONNX models and .ini
    │   └── test videos
    └── Frameworks/
        ├── Chromium Embedded Framework.framework
        └── SonoWorkstation Helper*.app
```

启动代码不依赖当前工作目录，而是从 `QCoreApplication::applicationDirPath()` 查找 Web 页面和运行时资产。

## 2. 标准启动命令

```bash
mise run run
```

完整任务链是：

```text
mise run run
  └─ build
       └─ configure
            └─ deps:sync          # CEF / ORT / ONNX 模型（Qt 由主机预装）
            └─ scripts/configure.py --type Release
       └─ cmake --build build
  └─ scripts/run_app.py           # 按平台启动构建产物
```

构建完成后，`scripts/run_app.py` 根据平台选择：

- macOS：`open build/SonoWorkstation.app`
- Linux：直接执行 `build/SonoWorkstation`
- Windows：查找 `build/SonoWorkstation.exe`、`build/Release/SonoWorkstation.exe` 或 `build/Debug/SonoWorkstation.exe`

也可以直接启动构建产物；前提是运行时资源已由 CMake 部署到可执行文件目录。

## 3. 进程模型

### CEF 子进程入口

`main()` 的第一件事是调用 `sonoCefExecuteProcess(argc, argv)`。

```text
可执行文件启动
  ├─ CEF Helper/Renderer/GPU 子进程 → 进入 SonoCefApp → 处理 CEF 子进程后直接退出
  └─ Browser 主进程 → 继续初始化 QApplication
```

因此，`--type=renderer` 等 CEF 内部参数由 CEF 先消费，不会进入 Qt 命令行解析。

### Browser 主进程

主进程依次完成：

1. 设置组织名、应用名、版本号、Windows 媒体后端和关闭垂直同步。
2. 创建 `QApplication` 并解析 `--bench`、`--size`、`--verify`。
3. 初始化 CEF：windowless rendering、透明背景、macOS external message pump、Windows/Linux 多线程消息循环。
4. 查找 `web/workstation.html`。
5. 启动 `CapturePipeline` 的采集线程和推理线程。
6. 创建 `MainWindow`、`CefHost`，并把本地 HTML URL 交给 CEF 创建离屏浏览器。
7. 显示主窗口并进入 Qt 事件循环。

## 4. 主窗口组成

`MainWindow` 构造时创建三层结构：

```text
QMainWindow
└── underlayHost (QWidget)
    ├── PreviewWidget (原生视频显示层，初始隐藏)
    └── OverlayWindow (透明、无边框、置顶的 CEF 输出层)
```

Web 页面加载后会调用 `setPreviewRect`，告诉 C++ 视频预览区域的 CSS 坐标；`HostBridge` 在宿主内部发出 `previewPlacement` 信号，`MainWindow::relayoutPreview()` 据此调整原生视频层和 CEF 透明窗口的位置。这样视频帧始终由 Qt 原生层显示，网页只负责透明 UI、测量和 AI 标注。

## 5. CEF 页面与桥接就绪

1. `CefHost` 为每个窗口设置 windowless 视图，并创建 `CefBrowser`。
2. CEF 渲染进程创建 V8 上下文时，`SonoCefApp::OnContextCreated()` 注入 `window.sono`、`window.__sonoPush` 和 `cefQuery` 封装。
3. Browser 端在 `OnAfterCreated()` 建立 `CefMessageRouter`，把查询交给 `MainWindow::handleSonoQuery()`。
4. 页面加载完成后，`CefHost::OnLoadEnd()` 注入透明样式，使 CEF 位图只显示 UI 和标注。
5. `CefHost::OnPaint()` 收到 BGRA 位图，转换为 `QImage`，通过回调更新 `OverlayWindow`。
6. `HostBridge` 把 C++ 信号编码为 `window.__sonoPush` 事件；Web 端用同名 Signal 对象分发。

协议字段和错误格式见 [C++ / Web 桥接协议](../specs/bridge-protocol.md)。

## 6. 采集与推理

`CapturePipeline` 构造时启动两个工作线程：

- **采集线程**：处理视频帧、ROI 裁剪和 Cine 缓存。
- **推理线程**：加载 ONNX Runtime 模型并执行推理，结果通过队列回到主线程。

模型和测试视频的路径由构建阶段复制到可执行文件目录，运行时通过 `findAsset()` 查找。应用首次启动时不会启动摄像头；需要 Web UI 或 `SelfTest` 发出 `start` 请求。

## 7. 事件循环与平台差异

| 平台 | CEF 消息循环 |
| :--- | :--- |
| Windows | `multi_threaded_message_loop = true` |
| Linux | `multi_threaded_message_loop = true` |
| macOS | `external_message_pump = true`，主窗口用 4 ms 定时器调用 `CefDoMessageLoopWork()` |

macOS 的定时泵用于确保 CEF 离屏渲染与 Qt 窗口重绘同步；其他平台由 CEF 自己处理消息循环。

## 8. 自检与基准模式

### `--verify`

启动后等待窗口和 CEF 就绪，由 `SelfTest` 依次验证设备枚举、测试视频、推理、冻结、Cine 和采集卡可用性。结果写入：

```text
<applicationDirPath>/selftest-webunderlay.json
<applicationDirPath>/sono.log
```

注意：当前 `SelfTest` 的甲状腺步骤依赖本机可用的 `我的甲状腺.mp4`，该视频尚未随仓库分发；干净环境中该项会失败。补齐测试资产或调整 `SelfTest` 的资产来源后，才能把这一项当作发布门禁。

### `--bench <seconds> --size WxH`

选择匹配分辨率的视频/采集源并记录采集、显示、推理 FPS 和推理延迟。结果写入：

```text
<applicationDirPath>/bench-webunderlay[-WxH].json
```

## 9. 退出流程

Qt 事件循环退出后，应用调用 `sonoCefShutdown()`，随后局部对象按逆序析构：`MainWindow`、`CapturePipeline`、CEF 主机对象和 Qt 应用。`CapturePipeline` 会停止采集并等待工作线程退出，避免线程访问已销毁的 UI 对象。

## 10. 启动失败排查

| 现象 | 优先检查 |
| :--- | :--- |
| 提示找不到 `workstation.html` | 构建是否覆盖了已有 cache；`build/.../web/workstation.html` 是否存在 |
| CEF 启动后立即退出 | `.deps/cef` 是否完整；macOS Helper.app 是否被复制并签名 |
| 窗口黑屏或没有 UI | 查看 `sono.log` 是否有 `cef first paint`；检查 CEF resources/locales |
| 找不到 ONNX 模型 | 运行 `mise run deps:sync`；确认 `deps/manifest.json` 的 `MODEL_REPO_URL` 指向正确仓库 |
| 页面存在但视频区域空白 | 检查 Web 端是否发送 `previewPlacement`，以及原生预览层是否可见 |
| IDE 启动行为与 CLI 不同 | 按 [IDE 与 mise 集成](ide-setup.md) 检查工具链和运行目录 |

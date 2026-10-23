# ADR-0001: 采用 Qt 原生视频 + CEF 离屏渲染混合架构

- **状态**：已采纳 (Accepted)
- **日期**：2026-10

## 背景与问题

超声智能工作站需要同时满足两项核心诉求：
1. **极低延迟与高帧率视频回显**：采集卡输入可达 1080p @ 60~240 FPS，若将每一帧视频编码或通过 IPC 传入浏览器 Canvas/Video 标签绘制，会产生显著的内存拷贝与帧率抖动。
2. **高度灵活的现代化交互 UI**：超声科医生需要丰富的测量工具、动态 ROI 框、AI 辅助标注与主题切换，使用现代 Web 技术栈开发 UI 的迭代效率远高于传统 Qt Widgets 手绘。

## 决策

采用 **WebUnderlay 混合架构**：
1. 底层视频流完全在 C++ 原生层流转（`CapturePipeline` -> `QVideoWidget`），利用硬件加速直出显示。
2. 上层交互界面由 CEF 以离屏渲染（Windowless / Off-Screen Rendering）模式加载 `web/workstation.html`，网页背景透明，仅绘制 HUD 控制面板、测量标注与 AI 识别框，合成叠加在 `OverlayWindow` 上。
3. C++ 与 Web 之间仅传输轻量的控制指令与结构化 JSON 坐标数据（通过 `cefQuery` 与 `window.__sonoPush`），**绝不跨桥传输视频帧像素**。

## 影响与收益

- **性能收益**：视频显示与网页渲染解耦，即使网页进行复杂 DOM 操作也不会阻塞原生超声视频流帧率。
- **跨平台一致性**：Windows、Linux、macOS 三端共用同一套 `web/workstation.html` UI 与交互逻辑。

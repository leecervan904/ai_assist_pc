# 超声智能工作站文档中心 (`docs/`)

本目录汇总 **超声智能工作站 (`SonoWorkstation`)** 的架构、接口、开发流程与工程规范。

---

## 文档导航地图 (Sitemap)

### 1. 架构设计 (`docs/architecture/`)
- [**系统总体架构与核心链路 (`overview.md`)**](architecture/overview.md)
  - 原生视频渲染 (`QVideoWidget`) + CEF 离屏透明覆盖层 (`OverlayWindow`) 混合架构详解、模块职责矩阵与跨平台进程模型。

### 2. 架构决策记录 (`docs/adr/`)
- [**ADR-0001：采用 Qt 原生视频 + CEF 离屏渲染混合架构**](adr/0001-qt-cef-underlay-architecture.md)
  - 记录为何放弃纯 Web 视频回显，选择原生 GPU 视频直出叠加 CEF 透明 UI。

### 3. 接口与模型契约 (`docs/specs/`)
- [**C++ / Web 桥接通信协议契约 (`bridge-protocol.md`)**](specs/bridge-protocol.md)
  - `window.sono`、`cefQuery` 与 `window.__sonoPush` 的双向消息格式规范。
- [**ONNX 算法模型接入契约 (`model-contract.md`)**](specs/model-contract.md)
  - 外部模型交付格式、`.ini` 配置约定与自动拉取流程；项目不发布模型制品。

### 4. 开发者指南 (`docs/guides/`)
- [**开发环境搭建与构建指南 (`development-setup.md`)**](guides/development-setup.md)
  - 基于 `mise`、`CMake`、`Ninja`、`uv` 的跨平台（macOS / Windows / Linux）一键上手教程。
- [**工具链与依赖配置 (`toolchain.md`)**](guides/toolchain.md)
  - 工具版本来源、主机 Qt、CEF/ORT 缓存、CMake 模块边界、升级与诊断流程。
- [**IDE 与 mise 集成 (`ide-setup.md`)**](guides/ide-setup.md)
  - CLion、VS Code、Xcode 如何加载 mise 环境，以及如何避免 IDE 绕过团队工具链。
- [**构建产物与启动链路 (`startup-process.md`)**](guides/startup-process.md)
  - 从可执行文件到 Qt 窗口、CEF 子进程、Web 页面、桥接协议和推理线程的完整启动过程。
- [**外部依赖与模型同步工作流 (`dependency-workflow.md`)**](guides/dependency-workflow.md)
  - 主机 Qt 前置条件、CEF/ORT 来源切换、模型仓库配置和版本升级流程。
- [**测试套件与放置约定 (`tests/README.md`)**](../tests/README.md)
  - C++、Python 与 Web 测试的目录和运行方式。
- [**研发协作、代码规范与提交规范 (`contributing.md`)**](guides/contributing.md)
- 目录契约、C++/Python 编码规范与 Conventional Commits 约定。

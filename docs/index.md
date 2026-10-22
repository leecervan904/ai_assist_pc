# 超声智能工作站文档中心 (`docs/`)

欢迎查阅 **超声智能工作站 (`SonoWorkstation`)** 结构化知识库。本目录采用 AI-Native 文档组织规范，同时面向人类工程师与 AI 编码助手提供清晰的上下文索引。

---

## 文档导航地图 (Sitemap)

### 1. 架构设计 (`docs/architecture/`)
- [**系统总体架构与核心链路 (`overview.md`)**](architecture/overview.md)
  - 原生视频渲染 (`QVideoWidget`) + CEF 离屏透明覆盖层 (`OverlayWindow`) 混合架构详解、模块职责矩阵与跨平台进程模型。

### 2. 架构决策记录 (`docs/adr/`)
- [**ADR-0001：采用 Qt 原生视频 + CEF 离屏渲染混合架构**](adr/0001-qt-cef-underlay-architecture.md)
  - 记录为何放弃纯 Web 视频回显，选择原生 GPU 视频直出叠加 CEF 透明 UI。
- [**ADR-0002：基于 GitHub Release 与按需 LFS 的大文件分级治理**](adr/0002-release-based-large-file-management.md)
  - 记录为何将 3GB+ 跨平台 CEF/ORT 及算法团队输出的 ONNX 模型迁移至 `deps/manifest.json` + GitHub Releases，仅对测试视频按需启用 Git LFS。

### 3. 接口与模型契约 (`docs/specs/`)
- [**C++ / Web 桥接通信协议契约 (`bridge-protocol.md`)**](specs/bridge-protocol.md)
  - `window.sono`、`cefQuery` 与 `window.__sonoPush` 的双向消息格式规范。
- [**ONNX 算法模型接入契约 (`model-contract.md`)**](specs/model-contract.md)
  - 算法团队交付 `.onnx` 模型与 `.ini` 配置文件的输入输出张量规范及版本发布流程。

### 4. 开发者指南 (`docs/guides/`)
- [**开发环境搭建与构建指南 (`development-setup.md`)**](guides/development-setup.md)
  - 基于 `mise`、`CMake`、`Ninja`、`uv` 的跨平台（macOS / Windows / Linux）一键上手教程。
- [**外部依赖与模型更新工作流 (`dependency-workflow.md`)**](guides/dependency-workflow.md)
  - 如何切换远程 Release 仓库变量、如何打包并升级 CEF 或 ONNX 模型版本。
- [**研发协作、代码规范与提交规范 (`contributing.md`)**](guides/contributing.md)
  - 目录契约、C++/Python 编码规范、Pre-commit 门禁与 Conventional Commits 约定。

# 超声智能工作站 (`SonoWorkstation`)

基于 **Qt 6 原生视频直出 + CEF 离屏透明网页 UI + ONNX Runtime 实时推理** 的跨平台超声智能辅助工作站（支持 Windows / Linux / macOS）。

---

## 1. 核心特性

- **低延迟视频链路**：支持超声采集卡与本地视频文件输入，由原生 `QVideoWidget` 进行 GPU 渲染。
- **离屏 Web UI 叠加**：通过 CEF 离屏渲染承载 `workstation.html`，负责超声测量、ROI、AI 框与交互控制。
- **实时 AI 推理**：内置 ONNX Runtime 推理引擎，支持甲状腺分割/检测 (`thyroid.onnx`) 与目标检测 (`XT-7-640.onnx`)。
- **现代工程化体系**：基于 `mise` 统一管理跨平台多语言工具链，通过 [`deps/manifest.json`](deps/manifest.json) 实现重型 SDK（官方源直拉至 `.deps/`）与算法模型（GitHub Releases）的按需同步与 SHA-256 校验。

---

## 2. 目录结构

```text
ai_assist_pc/
├── AGENTS.md               # AI Coding Agent 上下文入口与架构契约
├── mise.toml               # 跨平台多语言工具链与自动化任务定义
├── CMakeLists.txt          # C++20 / Qt6 / CEF / ONNX Runtime 构建定义
├── CMakePresets.json       # 标准跨平台 CMake 构建预设
├── pyproject.toml          # Python 工具链 (uv / ruff / pytest) 配置
├── deps/manifest.json      # 外部 SDK (CEF/ORT) 与 ONNX 模型版本及 Release 锁定清单
├── .deps/                  # 自动按需拉取的预编译平台 SDK (.deps/cef, .deps/onnxruntime)
├── assets/
│   ├── models/             # ONNX 模型文件 (.onnx 由 manifest 拉取) 与配置 (.ini)
│   └── media/              # 测试视频文件 (.mp4，按需由 Git LFS 管理)
├── third_party/            # 随 Git 跟踪的轻量第三方源码库（详见 third_party/README.md）
├── cmake/                  # 模块化 CMake 脚本 (CompilerWarnings, Sanitizers)
├── scripts/                # 依赖同步、打包与环境诊断 Python 脚本
├── tests/                  # C++ 与 Python 自动化测试套件
├── docs/                   # 结构化文档中心（详见 docs/index.md）
├── src/                    # C++ 核心源码
└── workstation.html        # 离屏渲染网页 UI
```

---

## 3. 快速开始（基于 `mise`）

```bash
# 1. 安装工具链 (CMake, Ninja, Python, uv, ruff, gh)
mise install

# 2. 同步当前操作系统匹配的外部依赖与模型，并检查环境
mise run setup

# 3. 编译项目 (Release)
mise run build

# 4. 启动应用
mise run run
```

> 若使用传统 CMake 命令行构建，请参考 [开发环境搭建与构建指南](docs/guides/development-setup.md)。

---

## 4. 文档导航

完整的技术架构、接口协议与工程规范请查阅 [**文档中心 (`docs/index.md`)**](docs/index.md)：

- [系统总体架构与核心链路](docs/architecture/overview.md)
- [ADR-0001：Qt 原生视频 + CEF 离屏渲染混合架构](docs/adr/0001-qt-cef-underlay-architecture.md)
- [ADR-0002：基于 GitHub Release 与按需 LFS 的大文件分级治理](docs/adr/0002-release-based-large-file-management.md)
- [C++ / Web 桥接通信协议契约](docs/specs/bridge-protocol.md)
- [ONNX 算法模型接入契约](docs/specs/model-contract.md)
- [外部依赖与模型更新工作流](docs/guides/dependency-workflow.md)
- [研发协作、代码规范与提交规范](docs/guides/contributing.md)

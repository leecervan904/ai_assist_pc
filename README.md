# 超声智能工作站 (`SonoWorkstation`)

基于 **Qt 6 原生视频直出 + CEF 离屏透明网页 UI + ONNX Runtime 实时推理** 的跨平台超声智能辅助工作站（支持 Windows / Linux / macOS）。

---

## 1. 核心特性

- **低延迟视频链路**：支持超声采集卡与本地视频文件输入，由原生 `QVideoWidget` 进行 GPU 渲染。
- **离屏 Web UI 叠加**：通过 CEF 离屏渲染承载 `web/workstation.html`，负责超声测量、ROI、AI 框与交互控制。
- **实时 AI 推理**：内置 ONNX Runtime 推理引擎，支持甲状腺分割/检测 (`thyroid.onnx`) 与目标检测 (`XT-7-640.onnx`)。

---

## 2. 目录结构

```text
ai_assist_pc/
├── AGENTS.md               # AI Coding Agent 上下文入口与架构契约
├── mise.toml               # 跨平台多语言工具链与自动化任务定义
├── CMakeLists.txt          # C++20 / Qt6 / CEF / ONNX Runtime 构建定义
├── CMakePresets.json       # 标准跨平台 CMake 构建预设
├── pyproject.toml          # Python 工具链 (uv / ruff / pytest) 配置
├── deps/manifest.json      # CEF/ORT 与可下载 ONNX 模型的版本、地址和校验声明
├── .deps/                  # 自动按需拉取的预编译平台 SDK (.deps/cef, .deps/onnxruntime)
├── assets/
│   ├── models/             # deps:sync 下载的 ONNX 模型 (.onnx, gitignore) 与配置 (.ini)
│   └── media/              # 测试视频文件 (.mp4，按需由 Git LFS 管理)
├── third_party/            # 随 Git 跟踪的轻量第三方源码库（详见 third_party/README.md）
├── cmake/                  # 依赖发现、打包、平台策略与编译选项
│   ├── packaging/          # macOS Helper 等平台打包模板
│   └── platform/           # Windows / macOS / Linux 平台实现
├── scripts/                # 依赖同步、打包与环境诊断 Python 脚本
├── tests/                  # C++ 与 Python 自动化测试套件
├── docs/                   # 结构化文档中心（详见 docs/index.md）
├── src/                    # C++ 核心源码
└── web/                    # CEF 离屏 Web UI 源码，构建后复制到运行时目录
    └── workstation.html
```

---

## 3. 快速开始（基于 `mise`）

> 前置条件：在主机手动安装 Qt 6.8.2+（含 Multimedia 组件）。`.onnx` 由 `deps:sync` 从 `deps/manifest.json` 的默认模型仓库自动下载，不提交到仓库；后续修改 `MODEL_REPO_URL` 即可切换仓库，临时覆盖可使用 `SONO_MODEL_REPO_URL` 或 `--model-repo-url`。详见 [开发环境搭建与构建指南](docs/guides/development-setup.md)。

```bash
# 1. 安装工具链 (CMake, Ninja, Python, uv, ruff, gh)
mise install

# 2. 同步 CEF/ORT、ONNX 模型与测试媒体，并检查环境
mise run setup

# 3. 编译项目 (Release)
mise run build

# 4. 启动应用
mise run run

# 可选：检查实际使用的 CMake、Ninja、Qt 和资源
mise run env:check
```

> 若使用传统 CMake 命令行构建，请参考 [开发环境搭建与构建指南](docs/guides/development-setup.md)。

---

## 4. 文档导航

完整的技术架构、接口协议与开发规范请查阅 [**文档中心 (`docs/index.md`)**](docs/index.md)：

- [系统总体架构与核心链路](docs/architecture/overview.md)
- [工具链与依赖配置](docs/guides/toolchain.md)
- [IDE 与 mise 集成](docs/guides/ide-setup.md)
- [构建产物与启动链路](docs/guides/startup-process.md)
- [ADR-0001：Qt 原生视频 + CEF 离屏渲染混合架构](docs/adr/0001-qt-cef-underlay-architecture.md)
- [C++ / Web 桥接通信协议契约](docs/specs/bridge-protocol.md)
- [ONNX 算法模型接入契约](docs/specs/model-contract.md)
- [外部依赖与模型同步工作流](docs/guides/dependency-workflow.md)
- [研发协作、代码规范与提交规范](docs/guides/contributing.md)

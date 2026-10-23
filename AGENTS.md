# AGENTS.md — AI Coding Agent Context & Engineering Guide

本文件为 AI 编码助手（Antigravity / Claude Code / Cursor 等）与开发者提供项目的核心架构契约、工具链指令与目录边界。

---

## 1. 项目定位与技术栈

- **项目名称**：超声智能工作站 (`SonoWorkstation` / `ai_assist_pc`)
- **核心架构**：**Qt6 原生视频底图 (`QVideoWidget`) + CEF 离屏渲染透明浮动层 (`OverlayWindow`) + ONNX Runtime 实时推理**
- **多语言技术栈**：
  - **C++20**：核心采集、推理、窗口合成与 C++/JS 桥接（`src/`）
  - **Python 3.12**：依赖拉取、环境诊断、构建运行编排与工程自动化（`scripts/`、`tests/python/`，由 `uv` + `ruff` 管理）
  - **Web (HTML/CSS/JS)**：超声测量、ROI、AI 标注框与交互界面（`web/workstation.html`）
  - **Toolchain Runner**：统一使用 **`mise`** 编排工具链与任务（见 `mise.toml`）

---

## 2. 常用命令速查 (Single Source of Truth)

| 操作目标 | 标准命令 (`mise`) | 底层等价命令 |
| :--- | :--- | :--- |
| **环境与依赖初始化** | `mise run setup` | `git lfs pull --include='assets/media/**' && python3 scripts/sync_deps.py && python3 scripts/check_env.py` |
| **同步 CEF/ORT 与 ONNX 模型** | `mise run deps:sync` | `python3 scripts/sync_deps.py` |
| **仅拉取测试媒体** | `mise run deps:lfs` | `git lfs pull --include='assets/media/**'` |
| **编译项目 (Release)** | `mise run build` | `python3 scripts/configure.py --type Release && cmake --build build --config Release` |
| **启动应用程序** | `mise run run` | macOS: `open build/SonoWorkstation.app` / Linux: `./build/SonoWorkstation` |
| **运行测试套件** | `mise run test` | 构建 C++ 测试后运行 `python3 scripts/run_tests.py`（Python unittest + CTest） |
| **代码静态检查** | `mise run lint` | `ruff check scripts/ tests/python/` |

> Qt 6.8.2+ 由用户主机手动安装。项目不提交或发布 `.onnx`，但 `deps:sync` 会从 manifest 中 `MODEL_REPO_URL` 的默认 GitHub Release 自动下载到 `assets/models/`；后续修改该字段即可切换仓库，临时覆盖可使用 `SONO_MODEL_REPO_URL` 或 `--model-repo-url`。

---

## 3. 目录职责与工程边界

```text
ai_assist_pc/
├── src/                  # C++ 核心源码（修改前须阅读 docs/architecture/overview.md）
├── deps/manifest.json    # CEF/ORT 与 ONNX 模型的版本、下载地址和校验声明（Qt 由主机安装）
├── .deps/                # 自动按当前系统拉取解压的预编译 SDK (.deps/cef, .deps/onnxruntime，被 gitignore)
├── third_party/          # 随 Git 提交的轻量级第三方源码库/Header-only 库
├── assets/
│   ├── models/           # deps:sync 下载的 ONNX 模型 (.onnx, gitignore) 与配置文件 (.ini 入 Git)
│   └── media/            # 跨平台测试视频资产 (.mp4，按需由 Git LFS 管理)
├── cmake/                # 依赖发现、资源编排、编译选项与平台策略
│   ├── packaging/        # macOS Helper 等平台打包模板
│   └── platform/         # Windows / macOS / Linux 构建与运行时部署
├── scripts/              # 跨平台 Python 工程自动化脚本
├── tests/                # C++ (QtTest/CTest) 与 Python (unittest/pytest) 测试套件
├── web/                  # CEF 离屏页面源码（不回传视频帧）
└── docs/                 # 结构化知识库（见 docs/index.md）
```

---

## 4. 核心架构不变量（AI 修改代码时必须遵守）

1. **严禁将视频流送入网页渲染**：视频帧必须始终由 C++ `CapturePipeline` 直送 `PreviewWidget` (`QVideoWidget`) 原生渲染，Web 层 (`web/workstation.html`) 只负责透明浮动层 UI、测量与标注框绘制。
2. **严禁向普通 Git 提交大二进制文件**：
   - 任何 `.onnx`、`.so`、`.dll`、`.dylib`、`.framework` 均不得直接 `git add` 入库；
   - 项目不上传或发布 ONNX 模型；`.onnx` 由 `deps:sync` 从配置的 GitHub Release 下载，配套 `.ini` 随 Git 维护；
   - 模型仓库默认值只在 `deps/manifest.json` 的 `MODEL_REPO_URL` 维护；临时覆盖使用 `SONO_MODEL_REPO_URL` / `--model-repo-url`。更新 CEF/ORT 时修改 manifest 的版本、URL、校验信息和 `verify_path`。
3. **遵循代码与提交规范**：
   - C++ 遵循 `.clang-format` 与 `.clang-tidy`；Python 遵循 `pyproject.toml` 中的 `ruff` 规则；
   - Git 提交遵循 Conventional Commits（详见 [docs/guides/contributing.md](docs/guides/contributing.md)）。

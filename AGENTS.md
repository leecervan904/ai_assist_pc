# AGENTS.md — AI Coding Agent Context & Engineering Guide

本文件为 AI 编码助手（Antigravity / Claude Code / Cursor 等）与开发者提供项目的核心架构契约、工具链指令与目录边界。

---

## 1. 项目定位与技术栈

- **项目名称**：超声智能工作站 (`SonoWorkstation` / `ai_assist_pc`)
- **核心架构**：**Qt6 原生视频底图 (`QVideoWidget`) + CEF 离屏渲染透明浮动层 (`OverlayWindow`) + ONNX Runtime 实时推理**
- **多语言技术栈**：
  - **C++20**：核心采集、推理、窗口合成与 C++/JS 桥接（`src/`）
  - **Python 3.12**：依赖拉取、模型校验、打包脚本与工程自动化（`scripts/`、`tests/python/`，由 `uv` + `ruff` 管理）
  - **Web (HTML/CSS/JS)**：超声测量、ROI、AI 标注框与交互界面（`workstation.html`）
  - **Toolchain Runner**：统一使用 **`mise`** 编排工具链与任务（见 `mise.toml`）

---

## 2. 常用命令速查 (Single Source of Truth)

| 操作目标 | 标准命令 (`mise`) | 底层等价命令 |
| :--- | :--- | :--- |
| **环境与依赖初始化** | `mise run setup` | `python3 scripts/sync_deps.py && python3 scripts/check_env.py` |
| **仅同步 CEF 与模型** | `mise run deps:sync` | `python3 scripts/sync_deps.py` |
| **编译项目 (Release)** | `mise run build` | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` |
| **启动应用程序** | `mise run run` | macOS: `open build/SonoWorkstation.app` / Linux: `./build/SonoWorkstation` |
| **运行测试套件** | `mise run test` | `python3 -m unittest discover -s tests/python -p "test_*.py" -v` |
| **代码静态检查** | `mise run lint` | `ruff check scripts/ tests/python/` |

---

## 3. 目录职责与工程边界

```text
ai_assist_pc/
├── src/                  # C++ 核心源码（修改前须阅读 docs/architecture/overview.md）
├── deps/manifest.json    # 外部重型 SDK (CEF/ORT) 与 ONNX 模型的版本、URL 模板、SHA-256 锁定清单
├── .deps/                # 自动按当前系统拉取解压的预编译 SDK (.deps/cef, .deps/onnxruntime，被 gitignore)
├── third_party/          # 随 Git 提交的轻量级第三方源码库/Header-only 库
├── assets/
│   ├── models/           # ONNX 模型 (.onnx 由 manifest 拉取并 gitignore) 与配置文件 (.ini 入 Git)
│   └── media/            # 跨平台测试视频资产 (.mp4，按需由 Git LFS 管理)
├── cmake/                # 模块化 CMake 工具脚本 (CompilerWarnings, Sanitizers)
├── scripts/              # 跨平台 Python 工程自动化脚本
├── tests/                # C++ (QtTest/CTest) 与 Python (unittest/pytest) 测试套件
└── docs/                 # 结构化知识库（见 docs/index.md）
```

---

## 4. 核心架构不变量（AI 修改代码时必须遵守）

1. **严禁将视频流送入网页渲染**：视频帧必须始终由 C++ `CapturePipeline` 直送 `PreviewWidget` (`QVideoWidget`) 原生渲染，Web 层 (`workstation.html`) 只负责透明浮动层 UI、测量与标注框绘制。
2. **严禁向普通 Git 提交大二进制文件**：
   - 任何 `.onnx`、`.so`、`.dll`、`.dylib`、`.framework` 均不得直接 `git add` 入库；
   - 更新模型或 SDK 时，请将其发布至 GitHub Release 并更新 `deps/manifest.json` 中的版本号与 `sha256`。
3. **遵循代码与提交规范**：
   - C++ 遵循 `.clang-format` 与 `.clang-tidy`；Python 遵循 `pyproject.toml` 中的 `ruff` 规则；
   - Git 提交遵循 Conventional Commits（详见 [docs/guides/contributing.md](docs/guides/contributing.md)）。

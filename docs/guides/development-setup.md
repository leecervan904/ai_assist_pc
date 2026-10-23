# 开发环境搭建与构建指南 (`development-setup.md`)

本项目使用 [mise](https://mise.jdx.dev) 统一管理构建工具和 Python 工具，使用 [`deps/manifest.json`](../../deps/manifest.json) 管理 CEF、ONNX Runtime 与 ONNX 模型的下载声明。Qt 6 由用户主机手动安装；模型文件不提交仓库，由 `deps:sync` 从配置的 GitHub Release 自动下载。

## 1. 前置条件

### 系统编译环境

- **macOS**：Xcode Command Line Tools (`xcode-select --install`)
- **Windows**：Visual Studio 2022，勾选“使用 C++ 的桌面开发”
- **Linux**：GCC 11+ 或 Clang 14+，以及 X11 开发库

```bash
# Debian / Ubuntu 示例
sudo apt install build-essential libx11-dev libxext-dev libxfixes-dev
```

### 安装 mise

- macOS / Linux：`curl https://mise.run | sh` 或 `brew install mise`
- Windows：`winget install jdx.mise`

### Qt 6（手动安装）

项目要求 **Qt 6.8.2 或更高版本**，并包含 `Core`、`Gui`、`Widgets`、`Network`、`Multimedia` 和 `MultimediaWidgets` 组件。Qt 由用户主机安装，`deps:sync` 不负责下载或安装 Qt。

- **macOS**：使用 Qt Online Installer，或执行 `brew install qt`。
- **Windows**：使用 Qt Online Installer，选择与 Visual Studio 2022 匹配的 64 位 MSVC 套件和 Qt Multimedia。
- **Linux**：使用 Qt Online Installer 或系统包管理器；若发行版提供的版本低于 6.8.2，请改用官方安装器。

安装完成后，CMake 会自动探测 Qt Online Installer、Homebrew 和平台常见目录。自定义路径时，在配置前设置其中之一：

```bash
# 让 CMake 从安装前缀查找 Qt（macOS / Linux 示例）
export CMAKE_PREFIX_PATH="$HOME/Qt/6.8.2/macos"

# 也可以在配置时直接指定 Qt6Config.cmake 所在目录
cmake --preset ninja-release -DQt6_DIR="$HOME/Qt/6.8.2/macos/lib/cmake/Qt6"
```

Windows 可把 `CMAKE_PREFIX_PATH` 设置为用户或系统环境变量，也可以在 CMake 配置中传入 `-DQt6_DIR=...`。CMake 配置阶段会打印实际选择的 Qt 版本和路径；运行 `mise run env:check` 可以检查本机是否已找到完整组件。

### ONNX 模型（自动下载）

模型文件由依赖同步任务拉取，不存放于本项目 Git 仓库。默认仓库写在 `deps/manifest.json` 的 `MODEL_REPO_URL`；如果之后改用独立模型仓库，直接修改该默认值。仅需临时覆盖时使用环境变量：

```bash
export SONO_MODEL_REPO_URL="https://github.com/<owner>/<model-repo>"
mise run deps:sync
```

`scripts/sync_deps.py` 会把 `.onnx` 下载到 `assets/models/`，并按 manifest 中的 SHA-256 校验；`.onnx` 已被 Git 忽略，配套 `.ini` 随仓库维护。临时切换仓库可使用 `python3 scripts/sync_deps.py --model-repo-url <url>`。模型格式与更新约定见 [ONNX 算法模型接入契约](../specs/model-contract.md)。

## 2. 一键初始化

```bash
# 1. 安装 mise.toml 锁定的工具版本
mise install

# 2. 同步 CEF/ORT、ONNX 模型和测试媒体，并打印诊断
mise run setup

# 3. 编译 Release
mise run build

# 4. 启动
mise run run
```

测试视频为 LFS 指针时，可单独执行：

```bash
mise run deps:lfs
```

## 3. 原生 CMake 构建

共享构建入口是根目录 [CMakePresets.json](../../CMakePresets.json)：

```bash
# macOS / Linux / Windows（Ninja 可用时）
cmake --preset ninja-release
cmake --build --preset ninja-release

# Windows Visual Studio 2022
cmake --preset vs2022
cmake --build --preset release
```

需要切换模型仓库或 CEF/ORT 下载镜像时，在当前 shell、IDE Run/Debug 环境或 CI 中设置 `SONO_MODEL_REPO_URL`、`SONO_CEF_BASE_URL` 或 `SONO_ORT_BASE_URL`。配置优先级和示例见 [外部依赖与模型同步工作流](dependency-workflow.md#2-manifest-变量与覆盖顺序)。

### CMake 目录职责

- 根 `CMakeLists.txt`：项目入口、选项、平台选择。
- `cmake/Dependencies.cmake`：Qt 发现、CEF 初始化、ORT 目标。
- `cmake/Packaging.cmake`：Web、模型和测试视频的运行时部署。
- `cmake/platform/`：Windows、macOS、Linux 差异。
- `src/CMakeLists.txt`：应用目标。
- `tests/CMakeLists.txt`：测试目标入口。

更完整的工具链说明见 [工具链与依赖配置](toolchain.md)。

## 4. IDE 构建与调试

IDE 可以使用原生 CMake 构建，但必须加载 mise 环境，并使用不包含个人 SDK 绝对路径的运行配置。按 IDE 分别配置：

- JetBrains：安装 `intellij-mise`，选择 `ninja-debug` 预设，Windows 使用 Visual Studio 工具链。
- VS Code：安装 `mise-vscode`，让 task/debug 终端使用登录 shell。
- Xcode：在 Build Phase 中通过 `mise exec` 执行工具。

详细步骤见 [IDE 与 mise 集成](ide-setup.md)。

## 5. 验证

```bash
mise run env:check
mise run test
```

启动后再检查：

```bash
tail -n 100 build/SonoWorkstation.app/Contents/MacOS/sono.log   # macOS
tail -n 100 build/sono.log                                      # Windows / Linux
```

构建输出、CEF 页面加载和桥接初始化顺序见 [构建产物与启动链路](startup-process.md)。

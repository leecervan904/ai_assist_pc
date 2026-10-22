# 开发环境搭建与构建指南 (`development-setup.md`)

本项目使用 **[mise](https://mise.jdx.dev)** 统一管理跨平台工具链（CMake、Ninja、Python、uv、Ruff、GitHub CLI）与工程任务。

---

## 1. 前置条件

1. **系统基础编译环境**：
   - **macOS**：Xcode Command Line Tools (`xcode-select --install`)
   - **Windows**：Visual Studio 2022（勾选“使用 C++ 的桌面开发”）
   - **Linux**：GCC 11+ / Clang 14+，以及 X11 开发库 (`libx11-dev`, `libxext-dev`, `libxfixes-dev`)
2. **Qt 6 环境**：
   - 安装 Qt 6.5+（含 `Core`, `Gui`, `Widgets`, `Network`, `Multimedia`, `MultimediaWidgets`, `Test` 模块）。
3. **安装 `mise`**：
   - macOS / Linux：`curl https://mise.run | sh` 或 `brew install mise`
   - Windows：`winget install jdx.mise`

---

## 2. 一键初始化与构建

克隆仓库后，在项目根目录执行：

```bash
# 1. 安装 mise.toml 中声明的构建与脚本工具链
mise install

# 2. 同步当前平台的外部依赖 (CEF / ONNX Runtime) 与 AI 模型，并诊断环境
mise run setup

# 3. 执行 Release 构建
mise run build

# 4. 启动应用
mise run run
```

---

## 3. 使用原生 CMake Presets 构建（可选）

如果不通过 `mise run build`，也可直接使用 `CMakePresets.json`：

```bash
# 先确保依赖就绪
python3 scripts/sync_deps.py

# macOS / Linux (Ninja)
cmake --preset ninja-release
cmake --build --preset ninja-release

# Windows (Visual Studio 2022)
cmake --preset vs2022
cmake --build --preset release
```

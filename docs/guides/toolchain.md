# 工具链与依赖配置 (`toolchain.md`)

本文说明 SonoWorkstation 的工具链来源、版本锁定位置、SDK 缓存、CMake 模块边界和日常诊断方式。

## 1. 配置的唯一来源

| 内容 | 唯一来源 | 说明 |
| :--- | :--- | :--- |
| 开发工具版本 | [`mise.toml`](../../mise.toml) | CMake、Ninja、Python、uv、Ruff、GitHub CLI |
| CEF / ONNX Runtime / ONNX 模型声明 | [`deps/manifest.json`](../../deps/manifest.json) | 版本、下载地址、目标平台、校验信息 |
| ONNX 模型制品 | 配置的 GitHub Release | 由 `deps:sync` 下载到 `assets/models/`；不进入 Git |
| CMake 最低版本、Qt 最低版本 | [`CMakeLists.txt`](../../CMakeLists.txt) | Qt 由主机手动安装；低于要求时在配置阶段直接失败 |
| 构建预设 | [`CMakePresets.json`](../../CMakePresets.json) | 跨平台共享预设；个人路径放本地 `CMakeUserPresets.json` |
| Python 工程约束 | [`pyproject.toml`](../../pyproject.toml) | Python 3.12、Ruff 规则、pytest 配置 |

原则是：**版本只在一个地方声明，其他文件不复制具体版本号。** 例如不要在 IDE 运行配置、Shell 脚本或个人文档中再写死 `C:/Qt/6.x` 或某台机器的 CMake 路径。

## 2. 工具链分层

```text
操作系统编译环境
  └─ Xcode CLT / Visual Studio 2022 / GCC / Clang
       └─ mise 锁定 CMake、Ninja、Python、uv、Ruff
            ├─ 用户主机安装 Qt 6.8.2+ 与 Multimedia 组件
            └─ deps/manifest.json 锁定 CEF、ONNX Runtime 与模型下载声明
               └─ CMake 负责编译、链接、运行时资源编排
```

### 当前版本策略

- **CMake**：最低 3.28，团队版本固定为 3.28.6。
- **Python**：固定为 3.12 系列；`pyproject.toml` 要求 Python `>=3.12`。
- **Qt**：最低 6.8.2；由用户主机手动安装，仓库不下载或固定具体 Qt 补丁版本。
- **CEF**：版本由 manifest 锁定，必须使用 `.deps/cef` 或显式 `-DCEF_ROOT=...`。
- **ONNX Runtime**：Windows/Linux 由 manifest 拉取；macOS 使用可被 `find_package(onnxruntime CONFIG)` 找到的安装。
- **ONNX 模型**：由 manifest 声明并从配置的 GitHub Release 自动下载；仓库不提交 `.onnx`，只跟踪配套 `.ini`。
- **Python 工具命令**：uv、Ruff、GitHub CLI 由 mise 固定版本，避免不同开发机生成不同结果。

具体版本值请直接查看 `mise.toml`、`CMakeLists.txt`（Qt 最低版本）和 `deps/manifest.json`，不要在本文件维护第二份版本清单。

## 3. SDK 与缓存目录

```text
项目目录/
├── deps/manifest.json        # CEF/ORT 与模型下载声明
├── .deps/                    # CEF、ONNX Runtime，gitignored
├── .cache/downloads/         # 下载缓存，gitignored
├── assets/models/            # deps:sync 下载的 .onnx + 仓库中的 .ini
├── assets/media/             # Git LFS 测试视频
└── build/                    # CMake 输出，gitignored

用户目录/
└── Qt/6.*/                   # 主机手动安装的 Qt（具体位置因安装方式而异）
```

Qt 由主机统一维护，CEF/ORT 采用项目级隔离：Qt 可通过 Qt Online Installer、Homebrew 或系统包管理器安装；CEF 与 ONNX Runtime 的版本和宿主平台强绑定，放在项目 `.deps/` 中便于清理、切版本和排查。`third_party/` 只存放随 Git 跟踪的轻量源码依赖，不存放下载的二进制 SDK。

## 4. CMake 模块边界

```text
CMakeLists.txt                 # 项目入口、选项、平台选择、子目录编排
CMakePresets.json              # 共享构建预设
cmake/
├── CompilerWarnings.cmake     # 编译器警告策略
├── Sanitizers.cmake           # ASan / UBSan
├── Dependencies.cmake         # Qt 发现、CEF 初始化、ORT 目标
├── Packaging.cmake            # Web、模型、测试视频运行时编排
├── packaging/macOS/           # macOS Helper 的 Info.plist 等打包模板
└── platform/
    ├── Windows.cmake          # MSVC/CEF/ORT/windeployqt
    ├── macOS.cmake            # CEF framework、Helper.app、codesign
    └── Linux.cmake            # RPATH、libcef、libonnxruntime
src/CMakeLists.txt             # 主程序目标
tests/CMakeLists.txt           # 测试入口，内部按语言分层
```

根 `CMakeLists.txt` 不承载具体平台实现。平台模块提供统一接口：

- `sono_platform_configure_target()`：平台相关头文件、宏和链接。
- `sono_platform_create_helpers()`：仅 macOS 存在的 Helper.app 构造。
- `sono_platform_deploy_runtime()`：构建后部署 CEF/ORT/Qt 运行时。

根 `CMakeLists.txt` 和 `CMakePresets.json` 是 IDE、CI 与 `cmake -S .` 的标准发现入口；可复用模块放在 `cmake/`，目标定义跟随 `src/` 与 `tests/`。个人机器路径放在已被 Git 忽略的 `CMakeUserPresets.json`，共享预设只包含跨平台可复现的生成器和构建类型。

## 5. 初始化与日常命令

```bash
# 安装 mise 中锁定的工具版本
mise install

# 拉取当前平台 CEF/ORT、ONNX 模型和测试媒体，并检查环境（需先安装 Qt）
mise run setup

# 配置 + 编译 + 启动
mise run build
mise run run

# 仅诊断工具链和资源
mise run env:check

# 测试
mise run test
```

不经过 mise 时，也可使用 CMake Presets：

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release

# Windows Visual Studio 2022
cmake --preset vs2022
cmake --build --preset release
```

直接使用 CMake 的前提是当前进程已经拥有正确的编译器、Qt、CEF 和 ONNX Runtime 环境。IDE 配置见 [IDE 与 mise 集成](ide-setup.md)。

## 6. 版本升级流程

1. 升级 CEF/ORT 时，修改 `deps/manifest.json` 中的 URL、版本和可用的 SHA-256；切换模型仓库时修改同一文件的 `MODEL_REPO_URL`，更新模型版本时同步修改标签与 SHA-256。Qt 版本要求变更时修改 `CMakeLists.txt` 中的最低版本。
2. 执行 `mise run deps:sync`，确认只拉取当前平台所需资产。
3. 执行 `mise run env:check` 和 `mise run test`。
4. 只有最低支持版本变化时，才同步修改根 `CMakeLists.txt` 和 `CMakePresets.json`。
5. 重新构建三个平台；至少检查 CEF 子进程、macOS Helper.app、Linux RPATH 和 Windows 部署目录。

不要通过修改开发者本机的 `PATH`、IDE SDK 路径或复制 SDK 到仓库根目录来完成升级。

## 7. 常见诊断

```bash
mise which cmake
mise which ninja
mise which python
cmake --version
ninja --version
cmake --preset ninja-debug
```

如果 IDE 中的 CMake、Qt 或 Python 与上述命令不一致，优先修复 IDE 环境注入，而不是修改项目构建文件。详见 [IDE 与 mise 集成](ide-setup.md)。

## 8. 已知边界

- macOS 的 ONNX Runtime 当前通过系统或 Homebrew 的 CMake package 查找，不在 manifest 的 Windows/Linux 下载路径中。
- Qt 由用户主机安装，`deps:sync` 不下载 Qt；CMake 只负责发现并校验最低版本和所需组件。
- ONNX 模型不由本项目发布；`deps:sync` 从配置的 GitHub Release 下载并校验到 `assets/models/`，CMake 与 `env:check` 再检查本地文件是否齐全。
- 如果 `assets/media/` 尚未拉取 LFS 文件，配置阶段会明确报错，避免生成一个缺少测试视频的半成品运行目录。

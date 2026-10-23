# IDE 与 mise 集成 (`ide-setup.md`)

项目不提交 IDE 专属的运行配置。CLI、CI 和自动化任务以 `mise run` / `mise exec --` 为准；IDE 是编辑和调试前端，但必须加载同一套工具链。

## 1. 基础原则

- `mise.toml` 是工具版本来源，`deps/manifest.json` 是 CEF/ORT 与 ONNX 模型的下载声明；Qt 由主机侧准备。
- IDE 从桌面启动时，可能不会继承终端中的 `mise activate` 环境。
- 不要依赖“从终端启动 IDE”作为团队规范；这在 Windows、Xcode 和图形化启动场景下不稳定。
- 不要提交包含个人绝对路径或特定机器 SDK 路径的 IDE 配置。
- 调试可以启动 IDE 直接构建出的二进制；不必用 `mise run` 包住调试器。

## 2. 首次配置

```bash
mise install
mise run setup
mise run env:check
```

`env:check` 会显示当前实际使用的 CMake、Ninja、Python、Qt 和资源路径。只有它通过后，才继续配置 IDE。

### 让 GUI IDE 看见 mise 工具

mise 官方要求：GUI 编辑器从登录 shell 读取环境。请把 shims 加入登录 shell 的启动文件。

```zsh
# ~/.zprofile 或对应的登录 shell 配置
eval "$(mise activate zsh --shims)"
```

Windows 将 `%LOCALAPPDATA%\mise\shims` 加入用户 `PATH`。修改后重启 IDE，必要时重新登录桌面会话。

> shims 只解决工具命令和通过工具启动的进程环境；编辑器自身读取的环境变量仍需要 IDE 插件或项目任务加载。

## 3. CLion / JetBrains

推荐组合：

1. 安装 [intellij-mise](https://github.com/134130/intellij-mise) 插件。
2. 项目使用根 [CMakePresets.json](../../CMakePresets.json) 的 `ninja-debug` 或 `ninja-release`。
3. CMake executable 使用 `mise which cmake` 的结果，或由插件注入 mise 环境。
4. Windows 工具链选择 Visual Studio；macOS 选择 Xcode/Clang；Linux 选择项目支持的 GCC/Clang。
5. 运行配置使用项目内相对路径和 CMake 预设。
6. 调试器环境由插件加载项目环境后即可直接启动构建产物。

如果插件不可用，CLion 可能绕过 mise。此时用以下命令核对诊断：

```bash
mise which cmake
mise which ninja
mise run env:check
```

## 4. VS Code

推荐组合：

1. 安装 [mise-vscode](https://marketplace.visualstudio.com/items?itemName=hverlin.mise-vscode)。
2. 在用户设置中让 task/debug 终端启动登录 shell，例如 macOS：

```json
{
  "terminal.integrated.automationProfile.osx": {
    "path": "/bin/zsh",
    "args": ["--login"]
  }
}
```

3. 构建任务调用 `mise run build`，或直接使用 CMake Presets。
4. 需要命令级环境时使用：

```bash
mise exec -- cmake --build build --config Debug
```

## 5. Xcode

Xcode 的 Build Phase 不会执行交互式 shell 配置。需要在 Build Phase 中使用 `mise exec`，并显式指定项目目录：

```sh
"$HOME/.local/bin/mise" --cd "$SRCROOT" exec -- <command>
```

如果打包或签名脚本依赖环境变量，应在 Xcode 的 Run/Scheme 中显式设置，而不能假设 `.zshrc` 已加载。

## 6. IDE 版本不一致的排查顺序

1. `mise run env:check` 是否通过？
2. `mise which cmake`、`mise which ninja` 与 IDE 的配置路径是否一致？
3. IDE 是否从登录 shell 启动，或是否启用了 mise 插件？
4. CMake 输出中的 `Qt6 <version>: <path>` 是否来自预期安装？
5. 清理 IDE 的 CMake cache 后重新选择 `ninja-debug` 预设。
6. 检查 `sono.log`，确认运行时使用的是 `build/SonoWorkstation.app` 或对应平台的构建产物。

工具链和依赖目录的完整说明见 [工具链与依赖配置](toolchain.md)。

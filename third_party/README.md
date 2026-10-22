# Third-Party Source Libraries (`third_party/`)

本项目参考 **OBS Studio** 的工业级依赖隔离规范，采用 **`.deps/` 与 `third_party/` 物理分离** 策略：

## 1. 职责边界

| 目录 | 存放内容 | 是否入 Git 仓库 | 获取方式 |
| :--- | :--- | :--- | :--- |
| **`/.deps/`** | 重型预编译平台 SDK（`.deps/cef`、`.deps/onnxruntime`） | **否**（`/.deps/` 整体在 `.gitignore` 中忽略） | 通过 `mise run deps:sync` 自动从官方 CDN / GitHub Releases 按当前宿主系统拉取解压 |
| **`/third_party/`** | 轻量级 C/C++ 源码库或 Header-only 库（如 `glad`, `nlohmann_json`, `spdlog` 等） | **是**（100% 由 Git 正常追踪） | 直接随源码提交或通过 Submodule / CMake 引入 |

## 2. 优势

1. **零 `.gitignore` 误伤风险**：`third_party/` 下不再需要任何复杂的忽略规则，未来在此目录下新增任何源码库都能被 Git 立即识别。
2. **清理构建不丢 SDK**：重型预编译包缓存在 `.deps/` 而非 `build/` 中，执行 `mise run clean` 或 `rm -rf build` 时不会误删几百 MB 的 CEF SDK。
3. **跨平台路径极简**：`CMakeLists.txt` 中 `CEF_ROOT` 统一指向 `${CMAKE_CURRENT_SOURCE_DIR}/.deps/cef`，`ORT_ROOT` 统一指向 `${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime`，彻底告别 `win/mac/linux` 多目录嵌套判断。

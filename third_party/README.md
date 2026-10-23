# Third-Party Source Libraries (`third_party/`)

`third_party/` 存放随 Git 跟踪的轻量级第三方源码；下载得到的平台 SDK 单独放在 `.deps/`。

## 1. 目录边界

| 目录 | 存放内容 | 是否入 Git 仓库 | 获取方式 |
| :--- | :--- | :--- | :--- |
| **`/.deps/`** | 重型预编译平台 SDK（`.deps/cef`、`.deps/onnxruntime`） | **否**（`/.deps/` 整体在 `.gitignore` 中忽略） | 通过 `mise run deps:sync` 自动从官方 CDN / GitHub Releases 按当前宿主系统拉取解压 |
| **`/third_party/`** | 轻量级 C/C++ 源码库或 Header-only 库（如 `glad`, `nlohmann_json`, `spdlog` 等） | **是**（100% 由 Git 正常追踪） | 直接随源码提交或通过 Submodule / CMake 引入 |

## 2. 使用规则

- 新增源码级依赖时，直接放入 `third_party/` 并由 Git 跟踪。
- 不提交 `.deps/`、`.onnx` 或解压后的动态库。
- CMake 通过 `CEF_ROOT` 和 `ORT_ROOT` 使用 `.deps/cef` 与 `.deps/onnxruntime`。

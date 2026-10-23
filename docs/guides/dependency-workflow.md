# 外部依赖与模型同步工作流 (`dependency-workflow.md`)

本项目只把**依赖声明**提交到 Git：CEF、ONNX Runtime（ORT）和 ONNX 模型的实际二进制由 [`deps/manifest.json`](../../deps/manifest.json) 驱动，按当前宿主环境同步到本地目录。Qt 6 由用户主机手动安装；`.onnx` 不提交或发布到本项目，由 `deps:sync` 从配置的 GitHub Release 自动下载。

## 1. 资产边界

| 资产 | 来源 | 落盘位置 | 是否提交 Git |
| :--- | :--- | :--- | :--- |
| Qt 6 | 用户主机安装（Qt Online Installer、Homebrew 或系统包管理器） | 主机安装路径，由 CMake 发现 | 否 |
| CEF | Spotify 官方 CEF CDN | `.deps/cef/` | 否 |
| ONNX Runtime | Microsoft 官方 Release（Windows / Linux） | `.deps/onnxruntime/` | 否 |
| ONNX 模型 | 配置的 GitHub Release | `assets/models/*.onnx` | 否 |
| 模型配置 | 与模型配套的 `.ini` | `assets/models/*.ini` | 是 |
| 测试视频 | Git LFS | `assets/media/*.mp4` | 仅 LFS 指针 |

`third_party/` 只允许存放随源码提交的轻量第三方库；不要把下载后的 SDK 或模型放回 `third_party/`。macOS 当前通过系统或 Homebrew 提供的 `onnxruntime` CMake package 发现 ORT，这一差异记录在 [工具链与依赖配置](toolchain.md#8-已知边界)。

### Qt 前置条件

- 安装 Qt 6.8.2 或更高版本，并包含 `Core`、`Gui`、`Widgets`、`Network`、`Multimedia` 和 `MultimediaWidgets` 组件。
- `scripts/sync_deps.py` 不下载或安装 Qt，`mise run deps:sync` 同步 CEF、ORT 与模型，但不处理 Qt。
- CMake 会优先使用 `Qt6_DIR` CMake 变量或 `CMAKE_PREFIX_PATH`，随后探测 Qt Online Installer 和 Homebrew 的常见安装目录。
- 安装方式与平台示例见 [开发环境搭建与构建指南](development-setup.md)。

### ONNX 模型同步

- 本项目不上传、不打包、不发布 `.onnx`；模型制品由外部模型仓库维护。
- `mise run deps:sync` 会把 manifest 中的模型下载到 `assets/models/`，并按声明的 SHA-256 校验。
- 默认模型仓库写在 manifest 的 `MODEL_REPO_URL`，后续切换仓库只需修改这一处；临时覆盖可用 `SONO_MODEL_REPO_URL` 或 `--model-repo-url`。
- `.onnx` 已被 Git 忽略；配套 `.ini` 随 Git 维护，并应与下载的模型版本匹配。
- `mise run env:check` 与 CMake 配置会检查下载后的模型文件是否齐全。

交付格式与输入输出约定见 [ONNX 算法模型接入契约](../specs/model-contract.md)。

## 2. Manifest 变量与覆盖顺序

`deps/manifest.json` 顶部的 `variables` 提供团队默认值。CEF/ORT 地址按以下优先级解析：

```text
环境变量 > deps/manifest.json > 脚本内置默认值
```

模型仓库按以下优先级解析：

```text
--model-repo-url > SONO_MODEL_REPO_URL > deps/manifest.json 中的 MODEL_REPO_URL > 当前 Git 仓库
```

支持的覆盖项：

| 变量 | 环境变量 | 命令行参数 | 用途 |
| :--- | :--- | :--- | :--- |
| `CEF_BASE_URL` | `SONO_CEF_BASE_URL` | 无 | CEF 归档根地址 |
| `ORT_BASE_URL` | `SONO_ORT_BASE_URL` | 无 | ORT 归档根地址 |
| `MODEL_REPO_URL` | `SONO_MODEL_REPO_URL` | `--model-repo-url` | 模型 GitHub 仓库默认值；为空时才推导当前仓库 |
| `MODEL_RELEASE_TAG` | `SONO_MODEL_RELEASE_TAG` | 无 | 模型 Release 标签 |

`CEF_VERSION`、`CEF_VERSION_URLENC` 以及各平台的 ORT 版本直接写在目标 URL 中，没有独立的环境变量映射。升级时应同时修改版本字段、目标 URL 和完成后的 `verify_path`，不要只改显示用的版本号。

`MODEL_REPO_URL` 只在 `deps/manifest.json` 维护；`pyproject.toml` 只管理 Python 工程依赖，不保存模型仓库地址。环境变量和命令行属于运行时覆盖，不需要在 Python 侧重复配置。

### 配置独立模型仓库

如果模型发布在独立 GitHub 仓库，直接修改 `deps/manifest.json` 中的 `MODEL_REPO_URL`：

```json
"MODEL_REPO_URL": "https://github.com/<owner>/<model-repo>"
```

只修改当前机器的来源时，可在 shell、CI Secret 或 IDE 运行环境中覆盖：

```bash
export SONO_MODEL_REPO_URL="https://github.com/<owner>/<model-repo>"

mise run deps:sync
```

也可以只对当前命令覆盖：

```bash
python3 scripts/sync_deps.py \
  --model-repo-url https://github.com/<owner>/<model-repo>
```

### 内网镜像示例

镜像必须保持与官方相同的目录层级。不要把个人镜像地址提交到 `mise.toml`：

```bash
export SONO_CEF_BASE_URL="https://mirror.example.com/cef"
export SONO_ORT_BASE_URL="https://mirror.example.com/onnxruntime/releases/download"

mise run deps:sync
```

## 3. 常用同步操作

```bash
# 同步当前平台所需 CEF/ORT 与模型；已完整的资产会跳过
mise run deps:sync

# 只打印将执行的下载动作
python3 scripts/sync_deps.py --dry-run

# 为其他平台准备 SDK（例如在 macOS 上检查 Windows 清单）
python3 scripts/sync_deps.py --platform windows-x64 --dry-run

# 强制重新下载 CEF/ORT 与模型
python3 scripts/sync_deps.py --force
```

Qt 不受 `deps:sync` 影响。升级 Qt 时直接更新主机安装，并清理或重新配置 CMake 构建目录。

CEF/ORT 当前使用 `SKIP_IF_LOCAL_EXISTS`，主要通过 `verify_path` 判断解压是否完整；模型使用 manifest 中的 SHA-256 校验。切换到内部制品库时，建议把真实 SHA-256 填入 manifest。

## 4. CEF / ONNX Runtime 升级

### CEF

1. 在 CEF 官方构建索引中确认目标平台的完整版本号。
2. 更新 `CEF_VERSION` 与 URL 编码后的 `CEF_VERSION_URLENC`；两者必须对应同一构建。
3. 检查五个平台的归档命名和 `verify_path` 是否仍然匹配。
4. 设置 `--force` 重新同步，执行 `mise run build` 并检查 CEF 首次渲染日志。

项目只通过 manifest 声明的 URL 同步 CEF；如需切换到内部制品库，同时修改 URL、`strip_components` 与校验信息。

### ONNX Runtime

1. 在 Microsoft 官方 Release 中选择目标平台归档和新版本号。
2. 更新对应平台 target 的 `url`、`version` 描述与 `verify_path`。
3. Windows/Linux 重新执行 `mise run deps:sync`；macOS 更新系统或 Homebrew 安装并由 CMake 重新查找。
4. 用 `mise run build` 验证链接，并启动一次推理流程确认动态库可加载。

## 5. 回滚与排障

- CEF/ORT 上游 Release 应保留，不要覆盖同名 tag 或资产；回滚时回退 `deps/manifest.json` 并重新同步。
- 出现 404 时先执行 `--dry-run`，确认 CEF/ORT URL 与模型仓库、Release 标签是否正确。
- 出现 SHA-256 不匹配时删除对应本地缓存或模型文件，再使用 `--force`；不要手工改 manifest 的哈希来绕过校验。
- 出现“SDK 已存在但配置失败”时，检查 manifest 的 `verify_path` 是否对应当前 SDK 版本，并删除不完整的 `.deps/<sdk>` 后重新同步。
- 出现模型缺失时，运行 `mise run deps:sync`；如果模型在独立仓库，先配置 `SONO_MODEL_REPO_URL`。
- GitHub API 限流或私有 Release 可设置 `GITHUB_TOKEN` / `GH_TOKEN`，脚本会在访问 `github.com` 时带上认证头。

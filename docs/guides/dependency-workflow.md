# 外部依赖与模型更新工作流 (`dependency-workflow.md`)

本项目的所有大体积二进制文件（CEF、ONNX Runtime、`.onnx` 模型）均通过 [`deps/manifest.json`](../../deps/manifest.json) 声明并托管于 GitHub Releases。

---

## 1. 切换远程仓库地址（变量覆盖）

默认情况下，`deps/manifest.json` 中的变量指向：
- `REPO_URL`: `https://github.com/leecervan904/ai_assist_pc`
- `MODEL_REPO_URL`: `https://github.com/leecervan904/ai_assist_pc`

当后续迁移至正式企业仓库或独立算法团队仓库时，有三种方式切换：

### 方式 A：直接修改 `deps/manifest.json` 顶部的 `variables`
```json
  "variables": {
    "REPO_URL": "https://github.com/your-org/sono-workstation",
    "MODEL_REPO_URL": "https://github.com/your-org/sono-ai-models",
    "CEF_RELEASE_TAG": "deps-cef-v1.0.0",
    "ORT_RELEASE_TAG": "deps-ort-v1.31.0",
    "MODEL_RELEASE_TAG": "models-v1.0.0"
  }
```

### 方式 B：通过环境变量或 `mise.toml` 覆盖（适合 CI/CD 或内网镜像）
```bash
export SONO_REPO_URL="https://github.com/your-org/sono-workstation"
export SONO_MODEL_REPO_URL="https://github.com/your-org/sono-ai-models"
mise run deps:sync
```

### 方式 C：通过命令行参数临时指定
```bash
python3 scripts/sync_deps.py --repo-url https://github.com/your-org/sono-workstation
```

---

## 2. 打包并上传本地资产到 GitHub Release

如果你在本地更新了模型或某个平台的 CEF，可以运行内置打包脚本：

```bash
# 仅归档模型并计算 SHA-256
python3 scripts/package_deps.py --models-only

# 同时将 macOS CEF 打包为 .tar.xz 并输出 SHA-256
python3 scripts/package_deps.py --pack-mac-cef
```

打包产物会输出到 `dist/releases/`，随后使用 `gh` CLI 一键发布到远程 Release：

```bash
# 上传模型 Release
gh release create models-v1.0.0 dist/releases/*.onnx \
  --repo leecervan904/ai_assist_pc \
  --title "AI Models v1.0.0"

# 上传 CEF Release
gh release create deps-cef-v1.0.0 dist/releases/cef-* \
  --repo leecervan904/ai_assist_pc \
  --title "Prebuilt CEF SDK v1.0.0"
```

最后将终端输出的 `SHA-256` 填入 `deps/manifest.json` 即可。

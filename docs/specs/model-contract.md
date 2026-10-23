# ONNX 算法模型接入契约 (`model-contract.md`)

本文档规范算法团队与客户端工程团队之间的模型交付格式、元数据配置与自动拉取方式。项目不提交或发布 `.onnx` 文件；模型由外部 GitHub Release 托管，客户端通过 `deps:sync` 下载到本地。

---

## 1. 交付物清单

算法团队或模型仓库维护方提供以下内容：
1. **模型文件 (`<model_name>.onnx`)**：导出为标准 ONNX Opset 12~18 格式。
2. **配置文件 (`<model_name>.ini`)**：定义模型版本与类别启用开关。
3. **SHA-256 校验和**：用于在 `deps/manifest.json` 中锁定下载的模型文件。

`.onnx` 发布在模型仓库的 GitHub Release 中，不进入本项目 Git；配套 `.ini` 随本项目仓库维护。本项目不提供模型打包或上传脚本，也不承载模型 Release。

---

## 2. 当前已接入模型规格

| 模型标识 (`modelId`) | 模型文件 | 布局类型 (`Layout`) | 输入尺寸 (`W × H`) | 配置文件 |
| :--- | :--- | :--- | :--- | :--- |
| `bee-thyroid` | `assets/models/thyroid.onnx` | `ThyroidSeg` (分割+检测) | `512 × 512` (或模型定义尺寸) | `assets/models/thyroid.ini` |
| `xt-7-640` | `assets/models/XT-7-640.onnx` | `Yolov8` (目标检测) | `640 × 640` | `assets/models/XT-7-640.ini` |

---

## 3. 配置文件格式 (`*.ini`)

```ini
[Model]
Version=0

[Enabled]
0=1
1=1
2=1
3=1
4=1
5=1
```

---

## 4. 模型来源配置与更新流程

1. 确认外部模型仓库已发布所需 `.onnx`，并按需修改 `deps/manifest.json` 中的默认 `MODEL_REPO_URL`；临时覆盖可使用 `SONO_MODEL_REPO_URL` 或 `--model-repo-url`。
2. 在 `deps/manifest.json` 中维护模型下载条目；版本或制品变化时同步更新 `version`、`url` 与 `sha256`。
3. 运行 `mise run deps:sync`，脚本会下载并校验 `.onnx` 到 `assets/models/`。
4. 如 `.ini` 发生变化，评审后提交仓库；`.onnx` 保持 Git 忽略状态，不得提交。
5. 运行 `mise run env:check`，再执行 `mise run build` 和 `mise run test`。

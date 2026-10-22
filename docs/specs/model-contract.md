# ONNX 算法模型接入契约 (`model-contract.md`)

本文档规范算法团队与客户端工程团队之间的模型交付格式、元数据配置与版本同步标准。

---

## 1. 交付物清单

每次发布或更新模型时，算法团队在对应的 GitHub Release 中提供：
1. **模型文件 (`<model_name>.onnx`)**：导出为标准 ONNX Opset 12~18 格式。
2. **配置文件 (`<model_name>.ini`)**：定义模型版本与类别启用开关（存放于本仓库 `assets/models/`）。
3. **SHA-256 校验和**：用于填入 `deps/manifest.json`。

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

## 4. 模型版本升级流程

1. 算法团队在模型仓库发布新 Release（例如 `models-v1.1.0`），上传 `thyroid.onnx`。
2. 客户端工程在 [`deps/manifest.json`](../../deps/manifest.json) 中更新 `models.thyroid` 的 `version`、`url`（或 `MODEL_RELEASE_TAG`）以及新文件的 `sha256`。
3. 运行 `mise run deps:sync` 自动拉取并校验新模型，运行 `mise run test` 验证通过后提交 `deps/manifest.json` 变更。

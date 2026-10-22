# 研发协作、代码规范与提交规范 (`contributing.md`)

为保障多语言跨平台工程的代码质量与可维护性，所有贡献者（含 AI 编码助手）须遵循以下规范。

---

## 1. 代码风格与静态检查

| 语言 / 文件类型 | 格式化与检查工具 | 配置文件 | 执行命令 |
| :--- | :--- | :--- | :--- |
| **C++20 (`src/`, `tests/cpp/`)** | `clang-format` / `clang-tidy` | [`.clang-format`](../../.clang-format), [`.clang-tidy`](../../.clang-tidy) | `clang-format -i src/*.cpp src/*.h` |
| **Python (`scripts/`, `tests/python/`)** | `ruff` | [`pyproject.toml`](../../pyproject.toml) | `mise run lint` / `mise run format` |
| **通用文件排版** | EditorConfig | [`.editorconfig`](../../.editorconfig) | IDE 自动生效 |

---

## 2. Git 提交规范 (Conventional Commits)

提交信息必须遵循 [Conventional Commits](https://www.conventionalcommits.org/) 格式：

```text
<type>(<scope>): <subject>

[optional body]
```

### 常用 `type` 说明
- `feat`: 新增功能（如 `feat(inference): support dynamic batch size`）
- `fix`: 修复 Bug（如 `fix(cef): resolve overlay focus issue on X11`）
- `perf`: 性能优化（如 `perf(capture): reduce frame copy overhead`）
- `refactor`: 代码重构（不改变外部行为）
- `build`: 构建系统、CMake、`mise.toml` 或外部依赖版本更新（如 `build(deps): bump thyroid model to v1.1.0`）
- `docs`: 文档更新
- `test`: 添加或修改测试代码
- `chore`: 工程配置、脚本或辅助工具变动

---

## 3. Pre-commit 质量门禁

首次参与开发时，建议启用 `pre-commit` 自动钩子（会自动拦截超过 5MB 的非 LFS 大文件误提交，并检查代码格式与提交信息规范）：

```bash
pre-commit install
pre-commit install --hook-type commit-msg
```

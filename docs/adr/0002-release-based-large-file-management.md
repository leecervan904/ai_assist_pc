# ADR-0002: 基于 GitHub Release 与按需 LFS 的大文件分级治理

- **状态**：已采纳 (Accepted)
- **日期**：2026-10

## 背景与痛点

项目初期将 Windows、Linux、macOS 三端完整的 CEF SDK（累计超 3 GB，其中 Linux `libcef.so` 单文件达 1.5 GB）存入 Git LFS，同时将 `.onnx` 模型文件与 `.mp4` 测试视频直接提交进普通 Git 历史，导致：
1. **跨平台严重冗余**：macOS 开发者拉取仓库时被迫处理 Linux/Windows 的数 GB 二进制文件。
2. **Git 操作卡顿与故障**：`git status` / `checkout` 在对数百 MB 至 1.5 GB 文件执行 LFS clean/smudge filter 时耗时极长且易出现管道权限报错。
3. **跨团队模型协同困难**：ONNX 模型由算法团队高频训练迭代，直接混入客户端 Git 历史会导致仓库迅速膨胀。

## 决策

实施 **“代码与重型资产解耦、分级托管”** 策略：

1. **重型平台 SDK (`CEF`, `ONNX Runtime`) -> 官方源直拉 + `.deps/` 隔离（OBS Studio 模式）**：
   - 直接指向 Spotify 官方 CEF CDN 与 Microsoft 官方 ONNX Runtime GitHub Releases。
   - 在仓库中仅保留声明式清单 `deps/manifest.json`，通过 `scripts/sync_deps.py`（由 `mise run deps:sync` 触发）仅下载当前宿主平台所需的压缩包，自动剥离顶层目录并解压至 `.deps/cef` 与 `.deps/onnxruntime`。
   - 在 `.gitignore` 中直接忽略 `/.deps/`，而将 `third_party/` 纯粹保留给随 Git 提交的轻量源码级三方库。
2. **跨团队交付模型 (`*.onnx`) -> GitHub Releases + `deps/manifest.json`**：
   - 模型权重文件放入 `assets/models/` 并在 `.gitignore` 中忽略；
   - 在 `deps/manifest.json` 中记录算法团队（或主仓库）Release 的下载 URL 模板与 SHA-256 哈希；
   - 升级模型只需更新 `deps/manifest.json` 中的版本号与 SHA-256，实现零 Git 历史膨胀。
3. **轻量跨平台测试媒体 (`assets/media/*.mp4`) -> 按需 Git LFS**：
   - 测试视频体积小（3~12 MB）、全平台自测通用，保留在 `assets/media/` 并由 Git LFS 跟踪。

## 影响与收益

- Git 源码仓库体积缩小 95% 以上，克隆与日常 `git status` 秒级响应。
- 支持通过 `SONO_REPO_URL` / `SONO_MODEL_REPO_URL` 环境变量灵活切换远程制品仓库地址。

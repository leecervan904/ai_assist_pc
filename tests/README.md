# Testing Suite (`tests/`)

本项目测试覆盖 **C++ 核心逻辑**、**Python 工程工具与依赖清单** 以及 **端到端性能基准**。

## 目录结构

```text
tests/
├── cpp/                # C++ 单元与组件测试（基于 QtTest / CTest）
├── python/             # Python 工程脚本与依赖清单测试（unittest / pytest）
└── benchmarks/         # 采集帧率与推理延迟基准测试规范与归档
```

## 测试放置原则

- C++ 测试放在 `tests/cpp/`，由 `tests/CMakeLists.txt` 注册到 CTest。
- Python 测试放在 `tests/python/`，由 `pyproject.toml` 指定 pytest 与 Ruff 的检查范围。
- Web UI 当前是单文件 `web/workstation.html`；浏览器契约与端到端测试后续放在 `tests/web/`。
- Web UI 拆分为可独立导入的模块后，单元测试可以与源码同目录，并通过前端测试运行器发现。

## 运行方式

### 1. 使用 `mise` 一键执行全量测试
```bash
mise run test
```

该任务会先以 `-DSONO_BUILD_TESTS=ON` 配置并构建 C++ 测试，再运行 Python unittest 和 CTest；不依赖开发者事先手工配置测试目标。

### 2. 单独运行 Python 工具链与 Manifest 契约测试
```bash
python3 -m unittest discover -s tests/python -p "test_*.py" -v
# 或安装了 pytest 时:
pytest
```

### 3. 开启并运行 C++ 单元测试
```bash
cmake -S . -B build -G Ninja -DSONO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### 4. 运行应用内置全链路自检与帧率基准
```bash
# 全链路自检 (SelfTest)
./build/SonoWorkstation.app/Contents/MacOS/SonoWorkstation --verify

# 1280x720 12 秒 FPS 基准测试
./build/SonoWorkstation.app/Contents/MacOS/SonoWorkstation --bench 12 --size 1280x720
```

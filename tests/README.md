# Testing Suite (`tests/`)

本项目采用多语言分层测试架构，覆盖 **C++ 核心逻辑**、**Python 工程工具与模型契约** 以及 **端到端性能基准（Benchmarks）**。

## 目录结构

```text
tests/
├── cpp/                # C++ 单元与组件测试（基于 QtTest / CTest）
├── python/             # Python 工程脚本、依赖清单与 ONNX 模型算子契约测试（unittest / pytest）
└── benchmarks/         # 采集帧率与推理延迟基准测试规范与归档
```

## 运行方式

### 1. 使用 `mise` 一键执行全量测试
```bash
mise run test
```

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

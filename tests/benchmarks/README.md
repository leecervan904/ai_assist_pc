# Benchmarks (`tests/benchmarks/`)

本目录用于管理 **视频采集链路** 与 **ONNX 实时推理引擎** 的性能基准测试说明与基线数据。

## 基准指标定义

| 指标名称 | 目标基线 (1080p) | 说明 |
| :--- | :--- | :--- |
| **Capture FPS** | $\ge 60\text{ FPS}$ (采集卡支持至 120/240 FPS) | `CapturePipeline` 接收帧率 |
| **Display FPS** | $\ge 60\text{ FPS}$ | `PreviewWidget` (`QVideoWidget`) 渲染帧率 |
| **Inference Latency** | $\le 15\text{ ms}$ | `InferenceEngine` 单帧预处理 + ONNX 推理 + 后处理耗时 |

## 执行方式

```bash
# 运行 12 秒基准测试并输出 JSON 报告
./build/SonoWorkstation.app/Contents/MacOS/SonoWorkstation --bench 12 --size 1280x720
```

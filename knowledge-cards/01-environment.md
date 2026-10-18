# 知识卡 01：环境勘察

**日期**：2026-10-18　**状态**：已验证

## 结论

本机具备**编译**和 **Ascend C CPU 域调试**能力，**不具备真实 NPU 运行能力**。

| 项 | 实测值 |
| --- | --- |
| 工作目录 | `/mnt/workspace/gitCode/cann/asc-devkit/AsendC_tanh` |
| OS / CPU | Linux 5.10.0 (aarch64)，16 核，无 SMT，2.9 GHz |
| CANN | 9.0.0，`ASCEND_HOME_PATH=/home/developer/Ascend/cann-9.0.0` |
| 设备节点 | `/dev/davinci*`、`/dev/devmm_svm`、`/dev/hisi_hdc` **均不存在** |
| `npu-smi` | 命令不存在 |
| 模拟器 | `/home/developer/Ascend/cann-9.0.0/tools/simulator/Ascend910B1` 存在 |
| CPU 域调试库 | `toolkit/tools/tikicpulib/lib` 存在，含 `Ascend910B1/libcpudebug.so` |

## 影响

1. **模板默认路径错误**：`CMakePresets.json` 原为 `/usr/local/Ascend/cann-9.0.0`，
   与本机实际路径不符，第 1 步构建前必须改为 `/home/developer/Ascend/cann-9.0.0`。
2. **aclnn 测试跑不了**：`AclNNInvocation/` 依赖真实设备，`run.sh` 无法完成。
3. **性能无法测量**：CPU 域是功能模拟，没有 MTE2 队列和流水线时序语义，
   CPU 耗时不能用来证明 NPU 性能。

## 下次检查项

- 换到带 NPU 的实例后，确认 `npu-smi info` 的芯片型号，核对 `ascend910b` 是否匹配。
- 确认 CANN 版本与题面要求一致（题面为 9.0.0）。

# 知识卡 09：无卡环境的性能仿真工具勘察

**日期**：2026-10-21　**状态**：已验证（本机 CANN 9.0.0，ascend910b 目标）

## 问题

kernel 已通过 CPU 域数值验证（卡 08）。想在不依赖 NPU 的情况下获得
指令级性能画像（compute/move 占比、瓶颈定位），勘察了所有候选工具。

## 结论：910B kernel 的时序仿真无可用命令行入口

| 工具 | 支持范围 | 证据 |
| --- | --- | --- |
| `npu-simulator` 仓库（/mnt/workspace/npu-simulator） | 仅 Ascend950 | `docs/zh/quick_start/quick_install.md:65`："当前支持 Ascend950" |
| `cannsim` wheel（tools/simulator/bin） | 仅 Ascend950 | `cannsim.soc_info.SOC_INFO` 仅含 `'Ascend950'` |
| `msprof op`（msopprof） | 真机 | `-h` 自述 "by running on the board"；二进制 strings 无 simulator 痕迹 |
| `tools/simulator/Ascend910B1/` 模型库 | 910B ✅ 存在 | `libmodel_top.so` 等齐全，env LD_LIBRARY_PATH 已含它，**但无开放 CLI 驱动入口**（历史路径为 MindStudio 工程仿真运行） |

扩展面板 Simulation analysis 点 Start now 无响应与此一致。
**待验证**：扩展在完整 MindStudio 工程结构下能否驱动 910B1 模型（不影响考试主线）。

## cannsim 的实际工作方式（读源码确认，供以后用）

```python
# record() 核心就一行命令拼接：
export LD_LIBRARY_PATH=<ca_model_path>:$LD_LIBRARY_PATH && <user_app> > cannsim.log
# 然后 model2trace 把模型输出转指令 trace、出流水图报告
```

用法（对 Ascend950 kernel 有效）：
`pip install` wheel 后 `cannsim record <app> -s Ascend950 -o <dir> -g`。

## 教训

1. 「包里存在某 SOC 的模型库」≠「存在该 SOC 的可用仿真入口」——引擎和驱动
   入口要分开确认。
2. 勘察工具支持范围时，读源码里的 SOC 常量表（如 `SOC_INFO`）比读宣传文档快。

## 对学习路线的影响

- CPU 域验证层级封顶在卡 08（数值正确 + calCount 尾块 + guard）。
- 性能结论必须等真机：安装 run 包 → `AclNNInvocation` → `msprof op`。
- 无卡期间可做的替代：静态指令量级估算（搬运字节数 vs 计算指令数），
  标注为推断，不作性能结论。

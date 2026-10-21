# TanhCustom 学习知识卡

记录日期：2026-10-18
对应工程：`TanhCustom/`（Ascend C 中级认证题目）

> 本目录位于算子提交目录**之外**，不参与考试打包。
> 每张卡标注「已验证 / 待验证」，避免把推断当成结论。

## 索引

| 编号 | 主题 | 状态 |
| --- | --- | --- |
| [01](01-environment.md) | 环境勘察：CANN、CPU 域调试、无 NPU | 已验证 |
| [02](02-cpu-debug-mode-trap.md) | CPU 域调试的 mix / AIV 模式陷阱 | 已验证 |
| [03](03-tiling-semantics.md) | 模板 Tiling 参数的真实语义 | 已验证 |
| [04](04-alignment-and-divisibility.md) | 32 B 对齐与整除：两条硬约束 | 已验证（功能层面） |
| [05](05-identity-copy-experiment.md) | 身份拷贝闭环实验 | 已在仓库内验证 |
| [06](06-api-availability.md) | 高阶 Tanh API **存在**（含一次误判修正） | 源码确认，未编译验证 |
| [07](07-split-formula-teaching.md) | 尾块划分公式：教学复盘与错误模式 | 积木 1 已验证；kernel 落地后经卡 08 数值回归 PASS |
| [08](08-tanh-numeric-loop.md) | 高阶 Tanh 数值闭环（CPU 域） | CPU 域已验证；真机未验证 |

## 当前进度

- 第 1 步（环境 + 骨架编译）：完成，产物 `TanhCustom/build_out/custom_opp_ubuntu_aarch64.run`
- 第 2 步（身份拷贝闭环）：完成并落地。kernel TODO 已补齐为 `Muls(x, 1.0)` 占位，
  harness 在 `experiments/identity_copy/`，结果 `PASS (0/32768 bytes differ)`
- 第 3 步（尾块与 Tiling 健壮性）：**完成**。分块 kernel + 参数化 harness 落地，
  7 形状矩阵（16384/16400/16380/100/2050/31/1）全 PASS、guard 全 0（见卡 07）。
  下一步：第 2 阶段，用高阶 `AscendC::Tanh` 替换 `Muls` 占位并做数值比对
- 第 2 阶段（高阶 Tanh 数值闭环）：**CPU 域完成**。kernel 已替换为 `AscendC::Tanh`
  （带 calCount），真实工程 ascend910b 编译通过；数值回归 harness 在
  `experiments/tanh_numeric/`，7 形状全 PASS、最大误差 2.43e-4 < 1e-3（见卡 08）。
  下一步：真机闭环——安装 run 包、跑 `AclNNInvocation` 精度与性能（需 NPU 环境）

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
| [05](05-identity-copy-experiment.md) | 身份拷贝闭环实验 | 已在 /tmp 副本验证 |
| [06](06-api-availability.md) | 高阶 Tanh API **存在**（含一次误判修正） | 源码确认，未编译验证 |

## 当前进度

- 第 1 步（环境 + 骨架编译）：完成，产物 `TanhCustom/build_out/custom_opp_ubuntu_aarch64.run`
- 第 2 步（身份拷贝闭环）：**未落地到仓库**。工程内 kernel 仍有 7 处 TODO，
  实验代码在 `/tmp/tanh_step2`，结果 `PASS (0/32768 bytes differ)`
- 第 3 步（尾块与 Tiling 健壮性）：未开始

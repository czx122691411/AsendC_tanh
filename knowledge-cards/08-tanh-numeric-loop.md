# 知识卡 08：高阶 Tanh 数值闭环（CPU 域）

**日期**：2026-10-21　**状态**：CPU 域已验证；真机与 NPU 精度待验证

## 做了什么

1. kernel `Compute()` 把 `Muls(yLocal, xLocal, 1.0f, count)` 占位替换为
   `AscendC::Tanh(yLocal, xLocal, count)`（带 `calCount` 的无临时缓冲重载），
   头文件 `#include "math/tanh.h"`。
2. 新增 `experiments/tanh_numeric/`：复用 identity_copy 的分核窗口/覆盖/guard 检查，
   把字节比对换成主机侧 `std::tanh(float)` golden 的逐元素误差检查，容差 1e-3。
   输入改为 [-3.00, 3.00] 步长 0.01 循环（覆盖单调段和饱和段交界）。
3. 7 形状矩阵（16384/16400/16380/100/2050/31/1）全 PASS：
   覆盖 PASS、`bad=0`、guard 双侧为 0。
4. 真实工程 `TanhCustom/build.sh`（ascend910b 目标）编译通过，
   `custom_opp_ubuntu_aarch64.run` 正常产出。

## 回答的知识卡 06 待验证项

- CPU 域调试下高阶 `AscendC::Tanh` **可用**：g++ harness 直接编译运行成功。
- `calCount` 尾块**安全**：尾 15 / 4 / 1 全部只写有效元素（guard 为 0）。
- fp16 误差**满足 1e-3**：最大绝对误差 2.43e-4，出现在 |ref|≈0.986 处——
  这是 fp16 在 1~2 区间 ulp≈9.8e-4 的量化量级，符合预期而非 API 精度问题。

## 这次踩的两个 harness 坑（都已在代码中修复并注明）

1. **guard 比对不能按 half 读**：`memset(y, 0xAA, ...)` 后 guard 区按 half 读是
   `0xAAAA`（NaN），`y[j] != static_cast<half>(0xAA)` 恒真。guard 必须按
   原始字节（`uint8_t*`）比对。identity_copy 用的就是字节比对，改写时换成
   half 比较是退步。
2. **改编译 harness 时丢了 `memset(x, 0xCC, allocatedBytes)`**：输入 guard 区
   变成未初始化内存，`x_guard_bad=128` 假报警。现象是"全形状只有输入 guard
   失守、输出 guard 完好"，据此 1 分钟内定位到是 harness 初始化遗漏而非 kernel
   越界（kernel 只写 yGm；若真是写越界应是 y_guard 失守）。

## 层次划分（遵守学习约定）

- 本卡所有结论属于 **CPU 域调试** 层：证明数据通路、calCount 语义和 fp16 量化
  量级，不证明 NPU 上的数值与性能。
- 真机还欠：安装 run 包 → `AclNNInvocation/run.sh` → 精度比对 + profiler。

## 追记（2026-10-21，coverage37 回归）

- 学习者自建 `plot_tanh_distribution.py` 可视化 37 个固定 FP16 覆盖点（含 2^-14、
  饱和边界 4.5/4.50390625/4.5078125），随后 harness 输入切换为同表（周期 37）。
- 边界验证：tanh(4.50390625)=0.9997551 < 1-2^-12 舍入到 0.9995117；
  tanh(4.5078125)=0.9997571 过线舍入到精确 1.0。kernel 与主机参考两侧全部同侧
  （`same_fp16=YES`）。7 形状 `NUMERIC PASS`，max_err=2.434e-4 稳定落在 i=25
  （x=+4.50390625）；N=1（x=0）误差为 0。
- **identity_copy 实验退役**：kernel 换成真 Tanh 后，其字节比对（y==x）必然 FAIL，
  这不是缺陷。它的 PASS 依赖恒等占位 kernel，使命（分核/搬运/guard）已在第 2/3 步
  完成归档；不要为让它 PASS 而回退 kernel。判定基线移交给 tanh_numeric。
- 教训（导师自查）：宣布"恢复 harness 即可回归 PASS"前，应先确认被检对象
  （kernel 行为）是否已变更——基线判定 = harness × 被测对象，二者缺一不可。

## 下一步

真机闭环（需要具备 NPU 的环境）：
安装算子包、跑 `AclNNInvocation` 精度测试，与 CPU 域 golden 对齐；
之后才是 tmp buffer 重载与性能优化。

# 知识卡 10：真机精度闭环（910B3）

**日期**：2026-10-22　**状态**：真机 aclnn 精度已验证；37 点覆盖表真机回归、性能测量未做

## 环境

- 真机：Ascend **910B3**（`npu-smi` 确认 Health OK），`/dev/davinci6`
- CANN 9.0.0：`~/Ascend/cann-9.0.0`（`ascend-toolkit/latest` 软链指向它）
- kernel 编译目标 `ascend910b`；`SocVersion ascend910b` 覆盖 910B 系列，910B3 实测兼容

## 做了什么

1. `TanhCustom/build.sh` 编译 → `build_out/custom_opp_ubuntu_aarch64.run`（366 KB）
2. 执行 `.run` 自解压安装到 `~/Ascend/cann-9.0.0/opp/vendors/customize/`（详细流程见
   [编译安装流程复盘](../docs/编译安装流程复盘.md)）
3. `AclNNInvocation/run.sh`：生成随机输入 → 编译调用端 → 真机执行 → 精度比对
   → **`test pass` / `you have passed the Precision!`**

## 途中一个问题（1 分钟定位）

`run.sh` 首跑报 `FileNotFoundError: './input/input_x.bin'`。

- **根因**：`.gitignore` 排除了 `input/`、`output/`，克隆下来的仓库没有运行目录，
  而 `run.sh` 假定它们存在（开头的 `rm ./input/*.bin` 也因此报 No such file）。
- **最小修复**：`mkdir -p input output`。
- **教训**：看报错别只看 traceback 最后一行——`run.sh` 最前面的两条 `rm` 失败
  就是同一根因的最早信号（第一个异常点原则）。

## 精度量化（随机 `[-3,3)` fp16，形状 `[8,2048]`）

| 指标 | 真机（本次） | CPU 域（卡 08） |
| --- | --- | --- |
| 最大绝对误差 | **9.77e-4** @ x≈-2.18 | 2.43e-4 @ \|x\|≈4.5 |
| 误差 > 1e-3 的元素 | 0 / 16384 | 0 / 16384 |
| 逐位一致 | 6537/16384（40%） | —（基线不同，不可比） |

## 关键认识：基线不同，误差不可直接对比

真机 max_err（9.77e-4）比 CPU 域（2.43e-4）大 4 倍，**这不是 kernel 精度劣化**：

- 本 harness 的 golden 由 **NumPy 在 fp16 下计算**（`np.exp(fp16数组)` 仍是 fp16，
  golden 自身带量化误差）；卡 08 用的是 float64 的 `std::tanh`。
- 9.77e-4 ≈ 2^-10，正是 tanh≈-0.975（|val|∈[0.5,1)，ulp=4.88e-4）处的 **2 ulp**——
  kernel 路径与 NumPy 路径的舍入差。
- 40% 逐位一致意味着 60% 的元素相差 ±1 ulp 量级：判据必须是**误差容限**
  （`verify_result.py` 的 atol/rtol=1e-3），而非逐字节比对。字节比对只适用于
  恒等运算（身份拷贝实验）。

## 验证层级（学习约定）

本卡结论属于 **真机 kernel 测试 + aclnn 端到端** 层：证明算子包安装、注册、
发现、tiling 下发、kernel 执行、精度达标的完整链路。**尚不包含**：

- 卡 08 的 37 点 FP16 覆盖表（含 ±4.5 饱和边界）未在真机回归——隐藏用例
  最可能埋边界，下一步优先。
- 性能数据未测（卡 09：910B 无仿真 CLI，需真机 `msprof op`）。

## 下一步

1. 把 37 点覆盖表输入喂给真机（可复用 `gen_data.py` 的 tofile 格式），验证
   ±1 舍入边界两侧 kernel 与主机参考是否仍同侧。
2. `msprof op` 采集 kernel 耗时，建立性能基线（供后续 tmp buffer 重载对比）。

## 附：遗留线索

- 安装时一条权限 WARNING（CANN 根目录无写权限，走兜底创建成功）。若报
  「找不到算子」，第一排查点是 `ASCEND_CUSTOM_OPP_PATH`。
- 此版本安装包不生成 `set_env.bash`，需手动 export op_api 的
  `LD_LIBRARY_PATH`（本次 run.sh 链路实际已打通，说明默认扫描 opp/vendors
  已生效；该 export 是显式兜底）。

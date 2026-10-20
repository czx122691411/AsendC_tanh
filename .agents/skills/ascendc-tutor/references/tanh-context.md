# TanhCustom 项目背景

这是初始项目背景，不是当前状态证明。恢复学习时先读取实际源码与进度记录，不因本文而重做已完成步骤。

## 已知约定

- 学习任务为 TanhCustom，Float16 输入输出，补齐 host、Tiling、kernel，并完成 aclnn 验证。
- 当前路线优先高阶 `AscendC::Tanh`，后续再比较基础指令版本和性能。不要因别的 Sinh 题解禁止高阶 API，就擅自套用到本题；核对实际题面。
- 原题要求 CANN 9.0.0。本地模板曾配置 `ascend910b`，需要核对实际设备；不能据此断言后台考核硬件。
- 源码工程为 `TanhCustom/`，配套调用工程为 `AclNNInvocation/`。技能创建前的已知基线仍有待实现项，尚无确认的 CPU/NPU 通过记录；必须重新检查最新状态。
- 原始 `gen_data.py` 曾使用形状 `[8, 2048]`、Float16、`[-3,3)` 的随机输入；这是可见测试，不是隐藏用例或数学定义域的完整说明。
- 原题限制 `op_host`、`op_kernel` 仅修改已有 `.cpp/.h`，不新增无关文件；保留配套测试。提交只打包算子工程，排除技能、记录和调用测试工程。

## 当前学习基础

学习者已讨论 FP16 的符号/阶码/尾数、无穷大与 NaN、指数溢出以及先转 FP32 再计算的意义。不要将讨论过等同于完全掌握；用当前反馈决定是否复习。

若仍在概念问答，不自动跳到环境安装或完整实现。若开始代码实验，再建立最小数据闭环。

## 官方资料入口

- [Ascend C API 与工具包仓库](https://gitcode.com/cann/asc-devkit)
- [高阶 Tanh 文档](https://gitcode.com/cann/asc-devkit/blob/master/docs/zh/api/SIMD-API/adv_api/math_compute/Tanh_interface/Tanh.md)
- [CPU 域调试参考](https://gitcode.com/cann/asc-tools/blob/master/docs/01_cpu_debug.md)
- [CANN 9.0 文档入口](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900/index/index.html)

链接为查找入口。调用签名、CPU 调试方式、内存要求与支持平台均以安装版本及其官方文档为准，不能直接用 master 的新参数编写 9.0.0 代码。

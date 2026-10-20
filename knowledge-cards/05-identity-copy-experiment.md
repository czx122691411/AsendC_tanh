# 知识卡 05：身份拷贝闭环实验

**日期**：2026-10-18（初版）/ 2026-10-19（重新落地）　**状态**：已在仓库内验证

## 目的

在不涉及 Tanh 数值的前提下，单独验证数据通路：

```
GM ──DataCopy──> UB ──Muls(x,1.0)──> UB ──DataCopy──> GM
```

覆盖点：8 核切分、核偏移、tile 偏移、双缓冲队列、逐字节一致性。

**身份拷贝是独立实验，不能冒充 Tanh 通过。** 它只证明搬运正确。

## 数据规格

| 项 | 值 |
| --- | --- |
| 形状 | `[8, 2048]`，fp16 |
| 总元素 / 字节 | 16384 / 32768 B |
| `blockDim` | 8 |
| 每核元素 / 字节 | 2048 / 4096 B |
| `tileNum × BUFFER_NUM` | 8 × 2 = 16 |
| 每次搬运 | 128 元素 = 256 B（32 B 对齐 ✓） |

## 偏移计算

```
核偏移      blockOffset = GetBlockIdx() × blockLength   → 核 n 从 n×2048 开始
tile 偏移   progress × tileLength                        → 第 i 轮从 i×128 开始
```

## 关键实现点

```cpp
// 输入输出窗口限定在本核切片
xGm.SetGlobalBuffer((__gm__ DTYPE_X*)x + blockOffset, blockLength);
yGm.SetGlobalBuffer((__gm__ DTYPE_Y*)y + blockOffset, blockLength);

// 队列与 UB 分配
pipe.InitBuffer(inQueueX,  BUFFER_NUM, tileLength * sizeof(DTYPE_X));
pipe.InitBuffer(outQueueY, BUFFER_NUM, tileLength * sizeof(DTYPE_Y));

// 占位计算，第 4 步替换为真正的 Tanh
AscendC::Muls(yLocal, xLocal, (DTYPE_Y)1.0, tileLength);
```

## 验证方法

逐字节比较，而不是浮点误差比较——身份拷贝必须完全一致：

```cpp
for (size_t i = 0; i < bytes; ++i) if (px8[i] != py8[i]) bad++;
```

## 结果

```
[SUCCESS][CORE_0][pid ...] exit success!
...
[SUCCESS][CORE_7][pid ...] exit success!
[CORE_0] SUCCESS (0/4096 bytes differ)
...
[CORE_7] SUCCESS (0/4096 bytes differ)
identity copy: PASS (0/32768 bytes differ)
```

每核 4096 B × 8 = 32768 B，说明核偏移与 tile 偏移都没有重叠或缺口。

## 本实验**没有**验证的部分

- host `TilingFunc` 只参与编译，**没有真正执行**（需要 NPU 或 gert 上下文）；
- 尾块与不整除形状未处理；
- 真实 NPU 精度与时序。

## 复现路径

代码已落地，harness 在算子目录之外（不进入考试提交）：

| 文件 | 作用 |
| --- | --- |
| `TanhCustom/op_kernel/tanh_custom.cpp` | 身份拷贝 kernel（`Muls(x, 1.0)` 占位） |
| `experiments/identity_copy/main.cpp` | CPU 域 harness：造数、启动 kernel、逐核比对 |
| `experiments/identity_copy/cpu_tiling_shim.h` | `REGISTER_TILING_DEFAULT` / `GET_TILING_DATA` 替代 |
| `experiments/identity_copy/build.sh` | g++ 编译 + 运行 |

```bash
bash experiments/identity_copy/build.sh
```

harness 直接构造 `TanhCustomTilingData`，没有经过 host `TilingFunc`；
后者的正确性仍需在能执行 tiling 的环境里单独验证。

编译时有两个容易漏掉的点：

- `-isystem $ASCEND_HOME_PATH/asc`：`asc/impl/basic_api/kernel_macros.h`
  会 `#include "impl/utils/sys_macros.h"`，只加 `asc/include` 不够。
- `-DDTYPE_X=half -DDTYPE_Y=half`：这两个类型名由真机编译流程注入，
  g++ 直接编译 kernel 时必须自己给。

## 下次检查项

- 当前实现假设 `totalLength % blockDim == 0` 且 `tileNum × BUFFER_NUM | blockLength`；
  非整除形状会静默丢尾数据，下一步要加入尾块处理，并用 `DataCopyPad` 验证。
- 第 4 步把 `Muls` 换成 Tanh 后，身份拷贝 harness 应保留为回归基线。

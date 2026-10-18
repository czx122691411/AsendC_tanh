# 知识卡 05：身份拷贝闭环实验

**日期**：2026-10-18　**状态**：已在 `/tmp` 副本验证，未落地到仓库

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
[SUCCESS][CORE_0] ... [SUCCESS][CORE_7]
identity copy: PASS (0/32768 bytes differ)
```

## 本实验**没有**验证的部分

- host `TilingFunc` 只参与编译，**没有真正执行**（需要 NPU 或 gert 上下文）；
- 尾块与不整除形状未处理；
- 真实 NPU 精度与时序。

## 复现路径

实验副本位于 `/tmp/tanh_step2`（临时，重启后可能丢失）。
若要在仓库内复现，见知识卡 02 的 harness 说明。

## 下次检查项

- 第 3 步加入尾块处理后，身份拷贝实验要能同时通过 `16384` 和 `非整除形状`。
- 第 4 步把 `Muls` 换成 Tanh 后，身份拷贝实验应保留为回归基线。

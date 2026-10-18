# 知识卡 03：模板 Tiling 参数的真实语义

**日期**：2026-10-18　**状态**：源码推导，功能层面已验证

## 模板代码

```cpp
// kernel
constexpr int32_t BUFFER_NUM = 2;

__aicore__ inline void Init(GM_ADDR x, GM_ADDR y, uint32_t totalLength, uint32_t tileNum)
{
    this->tileNum    = tileNum;
    this->blockLength = totalLength / AscendC::GetBlockNum();
    this->tileLength  = this->blockLength / this->tileNum / BUFFER_NUM;
}

__aicore__ inline void Process()
{
    int32_t loopCount = this->tileNum * BUFFER_NUM;
    for (int32_t i = 0; i < loopCount; i++) { CopyIn(i); Compute(i); CopyOut(i); }
}
```

## 关键关系

```
循环次数 loopCount = tileNum × BUFFER_NUM
每次搬运 tileLength = blockLength / (tileNum × BUFFER_NUM)
```

**`tileNum` 不是「tile 总数」，而是「每缓冲槽位的 tile 数」。**
真正决定划分粒度的是乘积。

代入 `totalLength = 16384`、`blockDim = 8`（`blockLength = 2048`）：

| `tileNum` | `BUFFER_NUM` | 循环次数 | 每次元素 | 每次字节 | 每队列 UB |
| --- | --- | --- | --- | --- | --- |
| 8 | 1 | 8 | 256 | 512 B | 512 B |
| **8** | **2**（模板默认） | **16** | **128** | **256 B** | **512 B** |
| 16 | 1 | 16 | 128 | 256 B | 256 B |
| 16 | 2 | 32 | 64 | 128 B | 256 B |
| 4 | 2 | 8 | 256 | 512 B | 1024 B |
| 1 | 1 | 1 | 2048 | 4096 B | 4096 B |

## 两个正交的设计维度

| 维度 | 物理含义 | 影响 |
| --- | --- | --- |
| `tileLength` | 单次 `DataCopy` 搬运量 | 摊薄 DMA 固定开销；首次结果延迟 |
| `BUFFER_NUM` | UB 内轮转缓冲个数 | 能否让搬运与计算重叠 |

- `BUFFER_NUM = 1`：严格串行 `搬 → 算 → 写 → 搬 → 算 → 写`，无重叠。
- `BUFFER_NUM = 2`：一个 buffer 在算，另一个可以同时被填充（ping-pong）。

固定 `tileNum` 时，`tileLength × BUFFER_NUM = blockLength / tileNum` 不变，
所以**理想情况下 UB 占用总量与 `BUFFER_NUM` 无关**。
（实际会因整数截断而略有出入，见知识卡 04。）

## 注意

- `AscendC::GetBlockNum()` 返回启动核数，不是元素数。
- `SetGlobalBuffer(ptr + blockOffset, blockLength)` 的第二个参数是**元素个数**，不是字节数。
- 模板把 `tileNum` 和 `BUFFER_NUM` 耦合在一个公式里，调 `BUFFER_NUM` 会连带改变
  `tileLength`，很容易踩到不整除或不对齐。

## 下次检查项

- 改 `BUFFER_NUM` 后，重新核对 `tileLength` 是否仍满足知识卡 04 的两条硬约束。
- 讨论「哪种配置更快」之前，先确保配置合法；快慢只能在真机 profiler 上测。

# 知识卡 04：32 B 对齐与整除——两条硬约束

**日期**：2026-10-18　**状态**：源码依据 + CPU 域实验已验证

## 结论

`DataCopy` 的长度必须同时满足两条硬约束，否则数据会错，而且**真机上不报错**。

```
约束 1（整除）    (tileNum × BUFFER_NUM) | blockLength
约束 2（对齐）    tileLength × sizeof(T) % 32 == 0
                  fp16 等价于 tileLength % 16 == 0
```

---

## 一、32 B 是什么

**MTE 搬运引擎的最小传输/寻址单位**，硬件里叫 `C0`。

源码依据（`kernel_utils_constants.h`）：

```cpp
const int32_t DEFAULT_C0_SIZE = 32;
const uint16_t ONE_BLK_SIZE = 32;
const uint32_t ONE_BLK_HALF_NUM = ONE_BLK_SIZE / B16_BYTE_SIZE;   // 16
```

`DataCopy` 内部（`kernel_operator_data_copy_intf_impl.h`）：

```cpp
ASCENDC_ASSERT((count % AscendCUtils::GetC0Count(sizeof(PrimType)) == 0), {
    KERNEL_LOG(KERNEL_ERROR,
        "...count * sizeof(T) must be 32B align, current count value is %u. "
        "In NPU mode, no error is reported. The value is rounded down by 32B.", count);
});
repeatParams.blockLen = count / AscendCUtils::GetC0Count(sizeof(PrimType));
```

**`DataCopyParams` 的 `blockLen` / `srcGap` / `dstGap` 单位是 32 B 的块，不是字节。**
`count / 16` 一旦有余数，余数被静默丢弃。

### 为什么硬件要求 32 B

1. GM ↔ UB 的 DMA 通路按 256 bit（32 B）一拍传输，指令长度字段以「拍」计数。
2. GM 与 UB 的地址译码按 32 B 边界切块，一次传输覆盖整数个块。
3. 部分块写入需要 read-modify-write 才能不破坏相邻字节，基础 `DataCopy` 不做，
   所以只搬整块、余数丢弃。补齐由 `DataCopyPad` 显式完成。

### 这不是「原子性」

| | 原子性 | 32 B 对齐 |
| --- | --- | --- |
| 层面 | 并发语义 | 寻址 / 传输粒度 |
| 关心 | 别的核能否看到中间状态 | 一条指令覆盖多少字节 |
| 破坏后果 | 数据竞争 | 传输失败、余数丢失 |
| 类比 | `lock cmpxchg` | x86 `movaps` 要求 16 B 对齐 |

---

## 二、「16」是推导值，不是 fp16 位宽

```
16 = 32 B / 2 B
```

两个 16 是巧合。同样 32 B 下：

| dtype | 字节/元素 | 每块元素数 |
| --- | --- | --- |
| fp32 | 4 | 8 |
| fp16 / bf16 | 2 | 16 |
| int8 | 1 | 32 |
| fp4 / int4 | 0.5 | 64 |

改成 fp32 后，`tileLength` 必须是 8 的倍数。

---

## 三、违反时的两种表现

| 环境 | 表现 |
| --- | --- |
| **CPU 域调试** | `ASCENDC_ASSERT` 触发 `SIGABRT`，回栈指向 `DataCopyCheck` |
| **真机 NPU** | **不报错**，静默按 32 B 向下取整，结果错得无声 |

这是 CPU 域调试最大的价值：把真机上的静默错误变成可见的崩溃。

---

## 四、实测记录

例：`totalLength = 16384`，`blockLength = 2048`，fp16，`tileNum = 8`，`BUFFER_NUM = 3`

```
loopCount  = 8 × 3 = 24
tileLength = 2048 / 8 / 3 = 85      （整数截断，非 85.33）
85 × 2 B   = 170 B                  （不是 32 B 的倍数）

覆盖量 = 24 × 85 = 2040 < 2048      （每核丢 8 个元素）
```

CPU 域结果：

```
[ERROR][Block_3][AIV_0][...][DataCopyCheck][...]
  count * sizeof(T) must be 32B align, current count value is 85.
  In NPU mode, no error is reported. The value is rounded down by 32B.
identity copy: FAIL (32687/32768 bytes differ)
```

对照实验：同样 `BUFFER_NUM = 3`，只把 `totalLength` 改为 `8 × 384`（每核 384）：

```
loopCount = 24, tileLength = 16, 覆盖 384 / 384
identity copy: PASS (0/6144 bytes differ)
```

**结论：`BUFFER_NUM = 3` 本身合法，失败原因是形状适配（整除 + 对齐）。**

---

## 五、为什么「向上/向下取整」都不行

```
向下取整 85：24 × 85 = 2040   尾部 8 个元素从未处理 → 静默丢数据
向上取整 86：24 × 86 = 2064   最后一轮越界读写 → 破坏相邻核数据
```

取整只能掩盖余数，不能解决余数。正确做法只有两条：

1. 让 `tileNum × BUFFER_NUM` 整除 `blockLength`，且商是 16 的倍数；
2. 显式处理尾块：最后一轮用 `min(tileLength, remaining)`，并用 `DataCopyPad` 搬运。

---

## 六、尾块工具：DataCopyPad

`DataCopyPad` 接受长度**以字节为单位**的 `DataCopyExtParams`
（区别于 `DataCopyParams` 的 32 B 块单位），并支持左右 padding：

```cpp
template <typename T>
__aicore__ inline void DataCopyPad(const LocalTensor<T>& dst,
                                   const GlobalTensor<T>& src,
                                   const DataCopyExtParams& dataCopyParams,
                                   const DataCopyPadExtParams<T>& padParams);
```

例：每核 375 元素 = `23 × 16 + 7`，可拆成 23 个整块 + 1 个 7 元素尾块
（7 × 2 = 14 B），用 `DataCopyPad` 搬运尾块。

## 下次检查项

- 每次改 `blockDim` / `tileNum` / `BUFFER_NUM` / 输入形状，重算两条约束。
- 形状不能整除时，确认尾块路径是否用 `DataCopyPad`，以及填充值是否会影响计算。
- 写回 GM 的尾块不能越界覆盖下一个核的数据。

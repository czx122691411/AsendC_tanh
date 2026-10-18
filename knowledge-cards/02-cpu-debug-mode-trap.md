# 知识卡 02：CPU 域调试的 mix / AIV 模式陷阱

**日期**：2026-10-18　**状态**：已复现、已修复、已验证

## 现象

在 `tikicpulib` 的 CPU 域调试里，用 `ICPU_RUN_KF(kernel, 8, ...)` 启动，
`GetBlockIdx()` 返回 0~15 而不是 0~7，超过 8 核的部分访问越界，
日志出现：

```
free(): invalid pointer
terminate called after throwing an instance of 'std::bad_alloc'
```

## 所属层次

框架模拟层（tikicpulib 的 KernelMode），不是算子逻辑错误。

## 证据

探针实验（`/tmp/blkprobe`）打印每个块的 `GetBlockNum()` / `GetBlockIdx()`：

```
不设 KernelMode：        slot 0..15 都有输出，blockIdx 到 15
SetKernelMode(AIV_MODE)：slot 0..7 有输出，blockIdx 0..7
```

源码依据（`dav_c220/kernel_operator_sys_var_impl.h`）：

```cpp
#if defined(ASCENDC_CPU_DEBUG) && ASCENDC_CPU_DEBUG == 1
    if ASCEND_IS_AIV {
        return block_idx * g_taskRation + sub_block_idx;   // 默认 taskRation 放大块数
    }
    return block_idx;
```

`ICPU_RUN_KF` 默认走 mix 模式，把 `blockDim=8` 模拟成 AIC + 多个 AIV。

## 修复

在 harness 里、启动 kernel 之前显式设置：

```cpp
AscendC::SetKernelMode(KernelMode::AIV_MODE);
ICPU_RUN_KF(tanh_custom, blockDim, x, y, (GM_ADDR)nullptr, (GM_ADDR)&td);
```

## 验证

设置后 8 个核、`blockIdx` 0~7，身份拷贝 `PASS (0/32768 bytes differ)`，
无 `ERROR` / `bad_alloc` / `invalid pointer`。

## 下次检查项

- 每次新写 CPU 域 harness，先确认 `GetBlockIdx()` 的取值范围与 `blockDim` 一致。
- 如果 kernel 里用了 `GetBlockNum()`，也要确认它返回的是 8 而不是被放大后的值。
- 这个陷阱只存在于 CPU 域调试；真机上 `blockDim` 语义就是核数。

## 附：CPU 域 harness 的 tiling 宏替代

编译器在真机编译时会自动生成 `REGISTER_TILING_DEFAULT` / `GET_TILING_DATA`
（见 `asc_op_compile_base/asc_op_compiler/get_op_tiling.py`），
CPU 域直接用 g++ 编译时这些宏不存在，需要自己提供：

```cpp
#pragma once
#include <cstring>
#define REGISTER_TILING_DEFAULT(T)
#define GET_TILING_DATA(tiling_data, tiling_arg)          \
    TanhCustomTilingData tiling_data;                     \
    std::memcpy(&tiling_data, (const void*)(tiling_arg),  \
                sizeof(TanhCustomTilingData))
```

注意 `ICPU_RUN_KF` 的参数必须能转成 `GM_ADDR`，
传 tiling 结构体时要写 `(GM_ADDR)&td`，否则模板推导报
`cannot convert 'TilingProbe*' to 'unsigned char*'`。

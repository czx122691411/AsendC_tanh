# 知识卡 06：高阶 Tanh API 存在（并修正一次误判）

**日期**：2026-10-18　**状态**：源码已确认；未编译验证

## 结论

CANN 9.0.0 **提供高阶 `AscendC::Tanh`**，头文件：

```
/home/developer/Ascend/cann-9.0.0/aarch64-linux/asc/include/adv_api/math/tanh.h
```

对应编译期可见路径（`compiler/ascendc/include/highlevel_api/lib` 是符号链接）：

```
compiler/ascendc/include/highlevel_api/lib -> ../../asc/include/adv_api
```

因此 kernel 里可以直接 `#include` 并调用，不必用 `Exp` 手工组合。

## 一次误判的记录

最初检索时我执行：

```bash
grep -rin "tanh" .../aarch64-linux/include/ascendc/
→ 0 命中
```

并据此推断「无高阶 Tanh API」。**这个结论是错的。** 原因：只看了 `include/ascendc/`
这一棵目录树，没有跟进 `compiler/ascendc/include/highlevel_api/lib` 这个指向
`asc/include/adv_api` 的符号链接。另外 `ls .../math/ | head -60` 在字母序
`floor.h` 处截断，后面的 `tanh.h` 没显示出来。

**教训**：检索目录时不要用 `head` 截断；判断「某 API 不存在」前，
要确认所有 include 根目录都覆盖到了。

## 支持的架构

```cpp
#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 2201 || __NPU_ARCH__ == 2002 ||
    __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 5102 || __NPU_ARCH__ == 3003 || __NPU_ARCH__ == 3113)
```

`ascend910b` → `__NPU_ARCH__ == 2201`，在支持范围内，走 `tanh_common_impl.h`。

## 可用重载（`__NPU_ARCH__ == 2201`）

```cpp
// 需要外部临时缓冲
template <typename T, bool isReuseSource = false>
void Tanh(const LocalTensor<T>& dst, const LocalTensor<T>& src,
          const LocalTensor<uint8_t>& sharedTmpBuffer);
void Tanh(const LocalTensor<T>& dst, const LocalTensor<T>& src,
          const LocalTensor<uint8_t>& sharedTmpBuffer, const uint32_t calCount);

// 不带临时缓冲
void Tanh(const LocalTensor<T>& dst, const LocalTensor<T>& src);
void Tanh(const LocalTensor<T>& dst, const LocalTensor<T>& src, const uint32_t calCount);
```

`T` 支持 `half` / `float`。头文件注释给出的公式：

```
Tanh(x) = (e^(2x) - 1) / (e^(2x) + 1)
```

## 临时缓冲大小从哪来

`tanh_tiling.h`（host 侧）：

```cpp
void GetTanhMaxMinTmpSize(const ge::Shape& srcShape, const uint32_t typeSize,
                          const bool isReuseSource,
                          uint32_t& maxValue, uint32_t& minValue);

void GetTanhTmpBufferFactorSize(const uint32_t typeSize,
                                uint32_t& maxLiveNodeCount, uint32_t& extraBuf);
```

关系：

```
maxTmpBuffer = maxLiveNodeCount × inputSize × typeSize + extraBuf
```

若使用带 `sharedTmpBuffer` 的重载，需在 kernel 里 `InitBuffer` 一块
`TBuf<VECCALC>` 并把对应 `LocalTensor<uint8_t>` 传进去。

## 关键约束

头文件注释明确写着：

> **src/dst Tensor must be 32B aligned**, and it doesn't allow
> src/dst/sharedTmpBuffer tensor address overlap.

这与知识卡 04 的 32 B 约束一致——高阶 API 并不会帮你绕过对齐要求。

## 待验证项

- [ ] `Tanh` 在 CPU 域调试（`ASCENDC_CPU_DEBUG`）下是否可用，还是只能真机跑
- [ ] `calCount` 非 16 倍数时的行为（是否内部处理尾块）
- [ ] fp16、输入 `[-3, 3)` 下与 numpy golden 的误差是否满足 1e-3
- [ ] 两种重载（带/不带 tmp buffer）的实际性能差异（需真机 profiler）

## 下一步

第 4 步实现 Tanh 时，**优先使用高阶 `AscendC::Tanh`**，而不是 `Exp` 手工组合。
若 CPU 域不支持高阶 API，则退回到 `Exp` 组合并在真机上验证。

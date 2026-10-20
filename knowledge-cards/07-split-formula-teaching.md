# 知识卡 07：尾块划分公式——教学复盘与错误模式

**日期**：2026-10-19　**状态**：积木 1（划分公式）已验证（`split_test ALL PASS`）；
尾块 kernel 尚未落地，7 形状矩阵回归待运行

## 背景

卡 05 的身份拷贝在 `16400`（每核 2050）上每核丢 2 元素；`16380` 会让
`tileLength=127` 触发对齐断言。本节目标：取消"形状必须整除且对齐"的假设——

```
核间：按 16 元素（32B）块均分，tail 归最后一核
核内：tileLength 向上取整到 16 的倍数，loopCount 由覆盖量决定
尾块：字节非 32B 整数倍时走 DataCopyPad
```

教学方式：拆成 5 块积木（划分公式 → 常量与防御 → Process 骨架 → 搬运二选一 →
比对与 guard），每块"先预测、再实验"。本卡记录**积木 1** 全程对话。

## 一、导师的问题与实验布置

1. **预测题**（替换代码前，三个形状）：
   (a) `16400`：core0 的 `coreLength / tileLength / loopCount`、末块大小与 API；
   (b) `16380`：core7 的 `coreLength` 与末块；
   (c) `100`：哪些核是空核、core7 处理几个元素。
2. **纸面填表**：`total=100, blockDim=8` 的 8 核 `{offset, length}`；
   Q1 空核 offset 相同为何合法；Q2 "tail 平分给前几个核"破坏哪条约束。
3. **写代码**：`ExpectedRange(idx, blockDim, total)` + `split_formula_test.cpp`
   （不变量循环：首尾相接、Σlength=total；加 3 个抽查 assert）。
4. **出口检测**：`total=16392` 的 8 核窗口、哪个核长度非 16 倍数、core7 末块 API。
5. **独立练习**：`total=100` core7 的
   `tileLength → loopCount → count → 字节 → API` 链。

## 二、学习者的回答（推断与猜测，原样记录）

- **预测题**：三个形状全部用旧均分公式 `coreLength = totalLength / blockDim`
  （得 2050 / 2047 / 12）；`loopCount` 答成 "128+1=129"；认为 DataCopyPad
  会 "4B 补全为 32B"；(c) 答 "每核都有空缺，core7 = 12+4 = 16"。
- **代码**：`ExpectedRange` 的中间量（chunks/per/rem/tail）计算与注释全对，
  但缺最后的组装与 `return`；assert 抄了模板里的 `?` 占位符当语法；
  抽查值 16400-core0 写成 **2046**。
- **填表 total=100**：8 个核的 offset/length **全对**。
- **Q1**（空核）：不清楚；**Q2**：猜"约束 1 和约束 3 都被破坏"（方向正确）。
- **出口检测 16392**：表格把每核 length 写成 16（块大小当核长）、offset 写成
  `X*16−1`（差一错误）；"哪个核非 16 倍数"答"最后一个" ✓；
  core7 末块 API 答 DataCopy ✗；主动提问"尾数给 core7 是否也不满足对齐"（好问题）。
- **独立练习 total=100 core7**：`AlignUp(⌈4/16⌉,16)=16`、`loopCount=1`、
  `count=min(16,4)=4`、8B、DataCopyPad——**全对**，并自己发现"槽大内容小"
  分支（UB 槽按 tileLength 分配，末块只用一部分）。

## 三、实验证明与纠正

### 3.1 一次假实验（数字指纹诊断）

7 个形状输出完全相同（每核 `4/4100`、总 `32/32800`）→ `totalLength=1` 时缓冲
只有 2 字节，不可能比较 32800 字节 → **harness 未接收 argv，kernel 未替换**。
教训：实验程序必须回显自己的输入（`total/blockDim/tileNum`），否则无法发现
"跑了但参数没进来"。

### 3.2 正确答案

| 形状 | 划分（chunks/tail/per/rem） | 关键量 |
| --- | --- | --- |
| 16400 | 1025/0/128/**1** | core0=2064，其余 2048；core0：tileLength=144，loopCount=15，末块 48 元素=96B，96%32=0 → **DataCopy**（不是 Pad） |
| 16380 | 1023/12/127/7 | cores0-6=2048，core7=**2044**（旧公式得 2047 会全局丢 4 元素）；core7：tileLength=128，loopCount=16，末块 124=248B → **Pad** |
| 100 | 6/4/0/6 | cores0-5=16，core6=**0**（空核），core7=4 |
| 16392 | 1024/8/128/0 | cores0-6=2048，core7=[14336,16392) len=**2056**；tileLength=144，loopCount=15，count=2056−14×144=**40**，80B → **Pad** |

### 3.3 概念纠正

- **loopCount** = `⌈coreLength / tileLength⌉`（覆盖量决定轮数），
  不是 `tileNum × BUFFER_NUM`（那是旧模板隐含"每圈装满"的来源，即 16400 的 bug），
  更不是 tileLength+1。
- **DataCopyPad 语义**：严格搬运 `blockLen` 字节；"按 32B 截断"是基础 DataCopy
  在真机上的静默行为，Pad 正是为绕开它而生，不做任何圆整。
- **起点 vs 终点对齐**：约束只作用于起点（它是 MTE 搬运首址，也是下一核的起点）；
  尾核的终点允许不对齐——它是最后一个，不对齐被压缩到有 Pad 兜底的位置。
- **空核合法**：`[96,96)` 是空区间，不拥有字节，与 core7 的 `[96,100)` 不冲突；
  kernel 侧 `Process` 首行 `return`，全程不做 0 长度搬运。
- **Q2 修正**：约束 3（tail 归一个核）是约束 1（起点对齐）的**推论**而非并列条款。
  反例 `16392` 平分 tail：每核 2049 → core1 起点 2049 元素 = 4098B，
  `4098 % 32 = 2`，且错位向后续所有核**传染**（每核起点 = 前核终点）。
- **2046 的来源**：`2050 − 4`——把旧实验丢的 4 **字节**当 4 **元素**减，单位混淆。

### 3.4 验证

- `split_formula_test.cpp`：6 形状 × 8 核不变量 + 3 抽查 → **ALL PASS**。
- 链式算法成型：
  ```
  tileLength = AlignUp(⌈coreLength/slots⌉, EPB)      ← 搬运粒度（对齐）
  loopCount  = ⌈coreLength/tileLength⌉               ← 轮数（覆盖量）
  count(i)   = min(tileLength, coreLength − i·tileLength)
  API        = (count × sizeof(T)) % 32 == 0 ? DataCopy : DataCopyPad
  ```

## 四、错误模式总结（下次检查项）

| 模式 | 案例 | 对策 |
| --- | --- | --- |
| 单位混淆 | 2046 = 2050 − 4（字节当元素） | 每个中间数标注单位 |
| 差一错误 | offset 写成上一核末元素下标 | 半开区间 `[s,e)`；`offset(k+1)=offset(k)+length(k)` |
| 旧公式残留 | loopCount = tileNum×BUFFER_NUM | 轮数由覆盖量决定 |
| API 语义 | 以为 Pad 会补全到 32B | Pad 只搬 blockLen 字节 |
| 纸面 vs 代码 | 手算错、已测试代码对 | 冲突时先怀疑纸面，用工具复核 |

## 复现路径

```bash
cd experiments/identity_copy
g++ -std=c++17 -Wall split_formula_test.cpp -o split_test && ./split_test
```

出口检测与独立练习的形状（16392 / 100）可加进 totals 列表或抽查 assert 复核。

## 下次检查项

- [x] 尾块 kernel（`coreOffset/coreLength` 版）落地 + harness 完成参数化后，
      跑 7 形状矩阵：16384（回归）/ 16400 / 16380 / 100 / 2050 / 31 / 1
      全 PASS 且 `guard_bad=0`。**2026-10-20 完成**，见下方"落地记录"。
- [x] 积木 2 三个预测：fp32 下 EPB=8，16400→chunks=2050/tail=0，
      core0/core1=2056、其余 2048（首答曾把 EPB 与 per 接反）；5/6/7 等不整除
      32 的 dtype 触发 `static_assert` 编译失败（>32B 也失败）；`tileNum=0`
      会沿 slots 使 DivCeil 除零，兜底为 1。
- [x] `dtype_contract.cpp` 两次运行：fp16/32/int8 → 16/8/32；取消 `Elem6`
      注释后编译失败并指向 static_assert（惰性实例化检查生效）。

## 落地记录（2026-10-20）

- 学习者手写 kernel 与 harness（提示模式），导师修 21 个编译错误 + 补 harness 截断的收尾。
  错误三类：拼写（`constexpt/stastic_*`/缺类型）、命名（`GetLaunchBlockNum` 不存在→
  `GetBlockNum`、三目冒号、未声明变量）、签名传导（`Compute/CopyOut` 未接住
  `(offset, count)`）。学习者自发亮点：`ParseU32` 健壮解析、防 harness 越界读的
  窗口检查、`[COVERAGE]/[GLOBAL]/[GUARD]` 三层独立汇总。
- 矩阵结果：7 形状全 PASS，`x/y_guard_bad=0`。**关键证据**：16380 的 core7 末块
  124 元素=248B 走 DataCopyPad 后 guard 完好 → CPU stub 的 Pad 严格只写
  `blockLen` 字节、无 32B 圆整（此前标"待验证"的项就此关闭，真机行为仍待 NPU）。
-学习者预测的 core7@16392 轮次表（15 轮、步进 144、末轮 40）与实测一致。

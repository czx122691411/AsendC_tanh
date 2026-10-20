// 身份拷贝的 Ascend C CPU 域 harness：验证分核、有效长度、搬运结果和输出尾部越界。
// 本文件不验证 Tanh 数值，也不能用 CPU 调试耗时证明 NPU 性能。
#include "tikicpulib.h"               // 引入 CPU 域调试、half、GmAlloc 和 ICPU_RUN_KF。
#include <cassert>                   // 为复用的 ExpectedRange 提供 assert。
#include <cerrno>                    // 检查 strtoul 是否发生范围错误。
#include <cstdint>                   // 提供 uint32_t、uint64_t、uint8_t 等整数类型。
#include <cstdio>                    // 提供 printf 和 fprintf。
#include <cstdlib>                   // 提供十进制字符串转换函数 strtoul。
#include <cstring>                   // 提供按字节初始化的 memset。
#include <limits>                    // 查询整数类型和 size_t 能表示的最大值。
#include "tanh_custom_tiling.h"       // 使用与 kernel 一致的 totalLength、tileNum 布局。

extern "C" __global__ __aicore__ void tanh_custom( // 声明将被 CPU 调试框架调用的 kernel。
    GM_ADDR x, GM_ADDR y,                        // 输入、输出的模拟 GM 地址。
    GM_ADDR workspace, GM_ADDR tiling);          // 本实验不用 workspace，tiling 指向参数结构体。

namespace {                                              // 将辅助常量和函数限制在本文件中。
constexpr uint32_t DEFAULT_TOTAL_LENGTH = 16384;           // 不传第一个参数时，处理 16384 个元素。
constexpr uint32_t DEFAULT_BLOCK_DIM = 8;                  // 不传第二个参数时，模拟 8 个执行实例。
constexpr uint32_t DEFAULT_TILE_NUM = 8;                   // 不传第三个参数时，使用分块调节参数 8。
constexpr size_t GUARD_ELEMENTS = 64;                     // 在输入、输出逻辑区之后各多分配 64 个元素。
constexpr unsigned char OUTPUT_SENTINEL = 0xAA;           // 输出逻辑区和 guard 都使用这个哨兵字节。
constexpr unsigned EPB = 32 / sizeof(half);                // FP16 时，一个 32B 完整块包含 16 个元素。
struct CoreRange { unsigned offset; unsigned length; };   // 用元素单位描述一个核的起点和有效长度。

// 函数体从 split_formula_test.cpp 复用，仅补充注释；EPB 在上面改为按 half 大小推导。
CoreRange ExpectedRange(unsigned idx, unsigned blockDim, unsigned total) // 计算当前核应负责的窗口。
{                                                        // 开始计算，不执行实际数据搬运。
    assert(blockDim > 0);                                 // 调用方必须传入非零核数。
    assert(idx < blockDim);                               // 核编号必须位于本次启动范围内。
    const unsigned chunks = total / EPB;                  // 计算可以分配的完整 32B 块数。
    const unsigned per = chunks / blockDim;               // 每个核基本分配的完整块数。
    const unsigned rem = chunks % blockDim;               // 剩余完整块分给前 rem 个核。
    const unsigned tail = total % EPB;                    // 不足一个完整块的元素数。
    CoreRange r;                                         // 准备保存当前核的窗口。
    r.offset = (idx * per + (idx < rem ? idx : rem)) * EPB; // 前面累计的完整块数乘每块元素数。
    r.length = (per + (idx < rem ? 1u : 0u)) * EPB         // 当前核的基本完整块和额外完整块。
             + (idx + 1 == blockDim ? tail : 0u);         // 只有最后一个核额外接收 tail 个元素。
    return r;                                            // 返回窗口，不修改输入输出内存。
}                                                        // ExpectedRange 结束。

bool ParseU32(const char* text, const char* name, uint32_t& value) // 解析单个命令行参数。
{                                                        // 所有参数只接受十进制非负整数。
    if (text[0] < '0' || text[0] > '9') {                  // 拒绝空串、负号、空白等开头。
        std::fprintf(stderr, "invalid %s: %s\n", name, text); // 说明哪个输入参数非法。
        return false;                                    // 让 main 返回非零状态。
    }                                                    // 输入首字符检查结束。
    errno = 0;                                           // 清除上次库函数调用留下的错误码。
    char* end = nullptr;                                 // 用于检查数字之后是否还有非法字符。
    const unsigned long parsed = std::strtoul(text, &end, 10); // 按十进制解析，保留结束位置。
    if (errno == ERANGE || end == text || *end != '\0'     // 拒绝溢出、未解析数字或尾随垃圾字符。
        || parsed > std::numeric_limits<uint32_t>::max()) { // 防止转换成 uint32_t 时发生截断。
        std::fprintf(stderr, "invalid %s: %s\n", name, text); // 输出错误参数及原始字符串。
        return false;                                    // 解析失败，不能进入 kernel 调用。
    }                                                    // 转换结果检查结束。
    value = static_cast<uint32_t>(parsed);                // 已确认范围合法，再保存结果。
    return true;                                         // 通知调用方解析成功。
}                                                        // ParseU32 结束。
}                                                        // 匿名命名空间结束。

int main(int argc, char** argv)                           // 程序入口，接收可选的三个形状参数。
{                                                        // 开始构造并验证一次身份拷贝实验。
    static_assert(sizeof(half) == 2, "half must be 2 bytes"); // 本 harness 使用 FP16 的两字节布局。
    uint32_t totalLength = DEFAULT_TOTAL_LENGTH;          // 总元素数先取默认值。
    uint32_t blockDim = DEFAULT_BLOCK_DIM;                // 启动实例数先取默认值。
    uint32_t tileNum = DEFAULT_TILE_NUM;                  // 分块调节参数先取默认值。
    if (argc > 4) {                                      // 程序名之外最多接受三个参数。
        std::fprintf(stderr, "usage: %s [totalLength [blockDim [tileNum]]]\n", argv[0]); // 给出位置顺序。
        return 2;                                        // 参数用法错误，不运行实验。
    }                                                    // 参数数量检查结束。
    if (argc > 1 && !ParseU32(argv[1], "totalLength", totalLength)) return 2; // 解析第一个参数。
    if (argc > 2 && !ParseU32(argv[2], "blockDim", blockDim)) return 2;       // 解析第二个参数。
    if (argc > 3 && !ParseU32(argv[3], "tileNum", tileNum)) return 2;          // 解析第三个参数。
    if (totalLength == 0 || blockDim == 0) {               // 本 harness 不接受空张量或零核启动。
        std::fprintf(stderr, "totalLength and blockDim must be greater than zero\n"); // 明确失败原因。
        return 2;                                        // 避免后续无意义启动或除零。
    }                                                    // 必要的非零检查结束。
    if (tileNum > std::numeric_limits<uint32_t>::max() / 2u) { // kernel 使用 tileNum*BUFFER_NUM，BUFFER_NUM 为 2。
        std::fprintf(stderr, "tileNum is too large for tileNum*2\n"); // 防止目标批次乘法溢出。
        return 2;                                        // 拒绝超出当前 kernel 算术合同的参数。
    }                                                    // tileNum=0 保留给 kernel 自己按 1 兜底。
    std::printf("totalLength=%u blockDim=%u tileNum=%u\n", totalLength, blockDim, tileNum); // 自报本次实际参数。

    const uint64_t allocElements64 = static_cast<uint64_t>(totalLength) + GUARD_ELEMENTS; // 先扩宽再加 guard。
    if (allocElements64 > std::numeric_limits<size_t>::max() / sizeof(half)) { // 检查字节数能否由 size_t 表示。
        std::fprintf(stderr, "allocation size overflow\n"); // 避免分配长度发生整数截断。
        return 2;                                        // 大小非法，不申请内存。
    }                                                    // 分配大小检查结束。
    const size_t logicalBytes = static_cast<size_t>(totalLength) * sizeof(half); // 逻辑数据区的字节数。
    const size_t allocatedBytes = static_cast<size_t>(allocElements64) * sizeof(half); // 含 guard 的总字节数。
    const size_t guardBytes = GUARD_ELEMENTS * sizeof(half); // 输出尾部 guard 大小，本例为 128B。
    half* x = static_cast<half*>(AscendC::GmAlloc(allocatedBytes)); // 申请输入逻辑区和尾部额外空间。
    half* y = static_cast<half*>(AscendC::GmAlloc(allocatedBytes)); // 申请输出逻辑区和尾部 guard。
    if (x == nullptr || y == nullptr) {                   // 任意一次申请失败，都不能继续访问。
        std::fprintf(stderr, "GmAlloc failed\n");          // 输出分配失败信息。
        if (x != nullptr) AscendC::GmFree(x);             // 释放可能已经成功申请的输入空间。
        if (y != nullptr) AscendC::GmFree(y);             // 释放可能已经成功申请的输出空间。
        return 2;                                        // 返回资源错误状态。
    }                                                    // 两份模拟 GM 已准备好。

    std::memset(x, 0xCC, allocatedBytes);                 // 输入尾部也初始化，避免保留未初始化字节。
    for (uint32_t i = 0; i < totalLength; ++i) {           // 只给真实输入逻辑区填写数值。
        x[i] = half(static_cast<int32_t>(i % 251u) - 125); // 生成 [-125,125] 的整数，FP16 可以精确表示。
    }                                                    // 输入逻辑区填写结束，额外空间保留 0xCC。
    std::memset(y, OUTPUT_SENTINEL, allocatedBytes);      // 输出逻辑区和 guard 全部预置为 0xAA。

    TanhCustomTilingData td{};                            // 创建与 kernel 一致的 tiling 参数结构体。
    td.totalLength = totalLength;                        // kernel 必须看到本次真正的总元素数。
    td.tileNum = tileNum;                                // 传入本次分块参数，零值由 kernel 兜底。
    AscendC::SetKernelMode(KernelMode::AIV_MODE);          // 本实验是向量核，CPU 调试显式选择 AIV 模式。
    ICPU_RUN_KF(tanh_custom, blockDim, (GM_ADDR)x, (GM_ADDR)y, // 用实际 blockDim 启动 CPU 域 kernel。
                (GM_ADDR)nullptr, (GM_ADDR)&td);          // workspace 未使用，tiling 指向本次参数。

    const uint8_t* px = reinterpret_cast<const uint8_t*>(x); // 将输入解释为字节，准备逐字节比对。
    const uint8_t* py = reinterpret_cast<const uint8_t*>(y); // 将输出解释为字节，不做浮点误差放宽。
    bool ok = true;                                      // 汇总窗口、数据和 guard 的检查结果。
    bool windowsOK = true;                               // 单独记录窗口连续性和范围是否合法。
    uint64_t next = 0;                                   // 记录下一个核预期的起点，单位是元素。
    uint64_t sum = 0;                                    // 累加所有核的有效长度，单位是元素。
    for (uint32_t core = 0; core < blockDim; ++core) {      // 逐核检查预期窗口和实际输出。
        const CoreRange r = ExpectedRange(core, blockDim, totalLength); // 复用已推演的分配公式。
        if (r.offset != next) {                          // 核窗口应按编号连续衔接，空窗口也允许。
            std::printf("[CORE_%u] FAIL offset=%u expected=%llu\n", core, r.offset, // 输出实际起点。
                        static_cast<unsigned long long>(next)); // 期望起点使用宽整数打印。
            ok = false;                                  // 起点不连续，即使数据正确也不能判通过。
            windowsOK = false;                           // 让 COVERAGE 汇总也包含起点连续性检查。
        }                                                // 当前核的连续性检查结束。
        next = static_cast<uint64_t>(r.offset) + r.length; // 更新为当前核窗口的末端。
        sum += r.length;                                 // 将本核有效长度加入覆盖总量。
        if (r.offset > totalLength || r.length > totalLength - r.offset) { // 防止错误窗口导致 harness 越界读。
            std::printf("[CORE_%u] FAIL window out of bounds\n", core); // 窗口本身已经不合法。
            ok = false;                                  // 记录失败，不能以局部字节正确抵消。
            windowsOK = false;                           // 非法窗口本身也是覆盖检查失败。
            continue;                                    // 跳过该非法窗口的内存读取。
        }                                                // 通过后才允许计算下面的字节范围。
        const size_t begin = static_cast<size_t>(r.offset) * sizeof(half); // 核起点由元素换算为字节。
        const size_t coreBytes = static_cast<size_t>(r.length) * sizeof(half); // 本核有效区间的字节数。
        size_t coreBad = 0;                               // 只统计当前核的错误字节。
        for (size_t j = begin; j < begin + coreBytes; ++j) { // 空核的 coreBytes 为零，因此不访问内存。
            if (px[j] != py[j]) ++coreBad;                // 身份拷贝要求对应字节完全一致。
        }                                                // 本核字节比对结束。
        if (coreBad != 0) ok = false;                     // 任意核数据错误，都影响最终退出状态。
        std::printf("[CORE_%u] %s offset=%u length=%u (%zu/%zu bytes differ)\n", // 自报该核覆盖范围。
                    core, coreBad == 0 ? "SUCCESS" : "FAIL", r.offset, r.length, coreBad, coreBytes); // 空核为 0/0。
    }                                                    // 所有核的预期范围检查结束。
    const bool coverageOK = windowsOK && sum == totalLength && next == totalLength; // 连续性、总量和末端均须通过。
    if (!coverageOK) ok = false;                          // 覆盖量错误直接使整个实验失败。
    std::printf("[COVERAGE] %s sum=%llu next=%llu expected=%u\n", // 显示两项独立的覆盖量。
                coverageOK ? "PASS" : "FAIL", static_cast<unsigned long long>(sum), // sum 使用元素单位。
                static_cast<unsigned long long>(next), totalLength); // next 也使用元素单位。

    size_t totalBad = 0;                                  // 独立统计整个逻辑区的错误字节。
    for (size_t j = 0; j < logicalBytes; ++j) {            // 不依赖 ExpectedRange 再扫描一次完整逻辑区。
        if (px[j] != py[j]) ++totalBad;                   // 防止错误的窗口划分隐藏未比对区域。
    }                                                    // 全局数据比对结束。
    if (totalBad != 0) ok = false;                        // 全局数据错也必须返回失败。
    std::printf("[GLOBAL] %s (%zu/%zu bytes differ)\n", // 输出全局结果，不将逐核计数重复相加。
                totalBad == 0 ? "PASS" : "FAIL", totalBad, logicalBytes); // 只统计有效逻辑区。

    size_t guardBad = 0;                                  // 统计输出尾部 guard 被改写的字节数。
    size_t xGuardBad = 0;                                 // 输入 guard 不应被 kernel 触碰。
    for (size_t j = logicalBytes; j < allocatedBytes; ++j) { // guard 区间独立于逻辑区比对。
        if (py[j] != OUTPUT_SENTINEL) ++guardBad;          // 输出 guard 被改写 = 写回越界。
        if (px[j] != 0xCC) ++xGuardBad;                    // 输入 guard 被改写同样是越界信号。
    }                                                     // guard 检查结束。
    if (guardBad != 0 || xGuardBad != 0) ok = false;       // 任一 guard 失守都判失败。
    std::printf("[GUARD] %s x_guard_bad=%zu y_guard_bad=%zu\n", // guard 单独成行，便于区分越界方向。
                (guardBad == 0 && xGuardBad == 0) ? "PASS" : "FAIL", xGuardBad, guardBad); // 两侧计数都打印。

    std::printf("identity copy: %s\n", ok ? "PASS" : "FAIL"); // 最终判定行；矩阵脚本以退出码为准。

    AscendC::GmFree(x);                                   // 释放模拟 GM 输入。
    AscendC::GmFree(y);                                   // 释放模拟 GM 输出。
    return ok ? 0 : 1;                                    // 退出码反映全部检查的综合结果。
}                                                         // main 结束。

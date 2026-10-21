// Tanh 数值回归的 Ascend C CPU 域 harness：验证高阶 AscendC::Tanh 的数值、
// 尾块 calCount 行为和输出尾部越界。分核窗口检查复用 identity_copy 的公式。
// CPU 域结果不能代表 NPU 精度与性能，真机回归仍需具备 NPU 的环境。
#include "tikicpulib.h"               // 引入 CPU 域调试、half、GmAlloc 和 ICPU_RUN_KF。
#include <cassert>                   // 为复用的 ExpectedRange 提供 assert。
#include <cerrno>                    // 检查 strtoul 是否发生范围错误。
#include <cmath>                     // 提供主机侧 golden 使用的 tanhf。
#include <cstdint>                   // 提供 uint32_t、uint64_t、uint8_t 等整数类型。
#include <cstdio>                    // 提供 printf 和 fprintf。
#include <cstdlib>                   // 提供十进制字符串转换函数 strtoul。
#include <cstring>                   // 提供按字节初始化的 memset。
#include <limits>                    // 查询整数类型和 size_t 能表示的最大值。
#include "tanh_custom_tiling.h"       // 使用与 kernel 一致的 totalLength、tileNum 布局。
#include <vector>

extern "C" __global__ __aicore__ void tanh_custom( // 声明将被 CPU 调试框架调用的 kernel。
    GM_ADDR x, GM_ADDR y,                        // 输入、输出的模拟 GM 地址。
    GM_ADDR workspace, GM_ADDR tiling);          // 本实验不用 workspace，tiling 指向参数结构体。

namespace {                                              // 将辅助常量和函数限制在本文件中。
constexpr uint32_t DEFAULT_TOTAL_LENGTH = 16384;           // 不传第一个参数时，处理 16384 个元素。
constexpr uint32_t DEFAULT_BLOCK_DIM = 8;                  // 不传第二个参数时，模拟 8 个执行实例。
constexpr uint32_t DEFAULT_TILE_NUM = 8;                   // 不传第三个参数时，使用分块调节参数 8。
constexpr size_t GUARD_ELEMENTS = 64;                     // 在输入、输出逻辑区之后各多分配 64 个元素。
constexpr unsigned char OUTPUT_SENTINEL = 0xAA;           // 输出逻辑区和 guard 都使用这个哨兵字节。
constexpr float TOLERANCE = 1e-3f;                        // 知识卡 06 约定：FP16、[-3,3] 下误差上限 1e-3。
constexpr unsigned EPB = 32 / sizeof(half);                // FP16 时，一个 32B 完整块包含 16 个元素。
struct CoreRange { unsigned offset; unsigned length; };   // 用元素单位描述一个核的起点和有效长度。

// 函数体从 identity_copy/main.cpp 复用，与 kernel 的分核公式保持一致。
CoreRange ExpectedRange(unsigned idx, unsigned blockDim, unsigned total) // 计算当前核应负责的窗口。
{
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
}

bool ParseU32(const char* text, const char* name, uint32_t& value) // 解析单个命令行参数。
{
    if (text[0] < '0' || text[0] > '9') {                  // 拒绝空串、负号、空白等开头。
        std::fprintf(stderr, "invalid %s: %s\n", name, text); // 说明哪个输入参数非法。
        return false;                                    // 让 main 返回非零状态。
    }
    errno = 0;                                           // 清除上次库函数调用留下的错误码。
    char* end = nullptr;                                 // 用于检查数字之后是否还有非法字符。
    const unsigned long parsed = std::strtoul(text, &end, 10); // 按十进制解析，保留结束位置。
    if (errno == ERANGE || end == text || *end != '\0'     // 拒绝溢出、未解析数字或尾随垃圾字符。
        || parsed > std::numeric_limits<uint32_t>::max()) { // 防止转换成 uint32_t 时发生截断。
        std::fprintf(stderr, "invalid %s: %s\n", name, text); // 输出错误参数及原始字符串。
        return false;                                    // 解析失败，不能进入 kernel 调用。
    }
    value = static_cast<uint32_t>(parsed);                // 已确认范围合法，再保存结果。
    return true;                                         // 通知调用方解析成功。
}
}                                                        // 匿名命名空间结束。

// 与 Python 脚本 COVERAGE37 完全相同，顺序也保持一致。
static const float TABLE[] = {
    // 零附近：9 个。
    0.0f,
     0.00006103515625f, -0.00006103515625f, // ±2^-14
     0.0009765625f,     -0.0009765625f,     // ±2^-10
     0.015625f,        -0.015625f,
     0.0625f,          -0.0625f,

    // 中间变化区：12 个。
     0.25f, -0.25f,
     0.5f,  -0.5f,
     0.75f, -0.75f,
     1.0f,  -1.0f,
     2.0f,  -2.0f,
     3.0f,  -3.0f,

    // 饱和边界附近：8 个。
     4.0f,        -4.0f,
     4.5f,        -4.5f,
     4.50390625f, -4.50390625f,
     4.5078125f,  -4.5078125f,

    // 明显饱和区：8 个。
     5.0f,   -5.0f,
     8.0f,   -8.0f,
     10.0f,  -10.0f,
     125.0f, -125.0f
};

constexpr size_t POINT_COUNT = sizeof(TABLE) / sizeof(TABLE[0]);
static_assert(POINT_COUNT == 37, "coverage table must have 37 points");

int main(int argc, char** argv)                           // 程序入口，接收可选的三个形状参数。
{
    static_assert(sizeof(half) == 2, "half must be 2 bytes"); // 本 harness 使用 FP16 的两字节布局。
    uint32_t totalLength = DEFAULT_TOTAL_LENGTH;          // 总元素数先取默认值。
    uint32_t blockDim = DEFAULT_BLOCK_DIM;                // 启动实例数先取默认值。
    uint32_t tileNum = DEFAULT_TILE_NUM;                  // 分块调节参数先取默认值。
    if (argc > 4) {                                      // 程序名之外最多接受三个参数。
        std::fprintf(stderr, "usage: %s [totalLength [blockDim [tileNum]]]\n", argv[0]); // 给出位置顺序。
        return 2;
    }
    if (argc > 1 && !ParseU32(argv[1], "totalLength", totalLength)) return 2; // 解析第一个参数。
    if (argc > 2 && !ParseU32(argv[2], "blockDim", blockDim)) return 2;       // 解析第二个参数。
    if (argc > 3 && !ParseU32(argv[3], "tileNum", tileNum)) return 2;          // 解析第三个参数。
    if (totalLength == 0 || blockDim == 0) {               // 本 harness 不接受空张量或零核启动。
        std::fprintf(stderr, "totalLength and blockDim must be greater than zero\n");
        return 2;
    }
    if (tileNum > std::numeric_limits<uint32_t>::max() / 2u) { // kernel 使用 tileNum*BUFFER_NUM。
        std::fprintf(stderr, "tileNum is too large for tileNum*2\n");
        return 2;
    }
    std::printf("totalLength=%u blockDim=%u tileNum=%u\n", totalLength, blockDim, tileNum); // 自报本次实际参数。

    const uint64_t allocElements64 = static_cast<uint64_t>(totalLength) + GUARD_ELEMENTS; // 先扩宽再加 guard。
    if (allocElements64 > std::numeric_limits<size_t>::max() / sizeof(half)) { // 检查字节数能否由 size_t 表示。
        std::fprintf(stderr, "allocation size overflow\n");
        return 2;
    }
    const size_t logicalBytes = static_cast<size_t>(totalLength) * sizeof(half); // 逻辑数据区的字节数。
    const size_t allocatedBytes = static_cast<size_t>(allocElements64) * sizeof(half); // 含 guard 的总字节数。
    half* x = static_cast<half*>(AscendC::GmAlloc(allocatedBytes)); // 申请输入逻辑区和尾部额外空间。
    half* y = static_cast<half*>(AscendC::GmAlloc(allocatedBytes)); // 申请输出逻辑区和尾部 guard。
    if (x == nullptr || y == nullptr) {                   // 任意一次申请失败，都不能继续访问。
        std::fprintf(stderr, "GmAlloc failed\n");
        if (x != nullptr) AscendC::GmFree(x);
        if (y != nullptr) AscendC::GmFree(y);
        return 2;
    }

    std::memset(x, 0xCC, allocatedBytes);                 // 输入尾部也初始化，guard 比对依赖已知字节。
    // 输入为 coverage37 的 37 个固定 FP16 点，周期 37 重复。
    for (uint32_t i = 0; i < totalLength; ++i) {
        x[i] = half(TABLE[i % POINT_COUNT]);
    }
    std::memset(y, OUTPUT_SENTINEL, allocatedBytes);      // 输出逻辑区和 guard 全部预置为 0xAA。

// ===== 在这里加入：打印前 6 个输入及 CPU 参考结果 =====
    const uint32_t showCount = totalLength < 6u ? totalLength : 6u;

    for (uint32_t i = 0; i < showCount; ++i) {
        const double input =
            static_cast<double>(static_cast<float>(x[i]));

        const double reference = std::tanh(input);

        std::printf(
            "[REF] i=%u x=%.10g tanh_reference=%.10g\n",
            i, input, reference);
    }
    // ===== 新增代码结束 =====


    TanhCustomTilingData td{};                            // 创建与 kernel 一致的 tiling 参数结构体。
    td.totalLength = totalLength;                        // kernel 必须看到本次真正的总元素数。
    td.tileNum = tileNum;                                // 传入本次分块参数，零值由 kernel 兜底。
    AscendC::SetKernelMode(KernelMode::AIV_MODE);          // 向量核，CPU 调试显式选择 AIV 模式。
    ICPU_RUN_KF(tanh_custom, blockDim, (GM_ADDR)x, (GM_ADDR)y, // 用实际 blockDim 启动 CPU 域 kernel。
                (GM_ADDR)nullptr, (GM_ADDR)&td);          // workspace 未使用，tiling 指向本次参数。

    // 只观察第一个周期中的指定点，避免重复打印。
    for (uint32_t i = 0; i < totalLength && i < 37u; ++i) {
        if (i < 3u || (i >= 25u && i <= 28u)) {
            const float input = static_cast<float>(x[i]);
            const float ref = std::tanh(input);
            const float got = static_cast<float>(y[i]);

            // 将 CPU 参考值也舍入成 half，便于检查边界舍入结果。
            const float roundedRef = static_cast<float>(half(ref));

            std::printf(
                "[POINT] i=%u x=%.10g got=%.10g "
                "ref=%.10g ref_fp16=%.10g err=%.10g same_fp16=%s\n",
                i,
                static_cast<double>(input),
                static_cast<double>(got),
                static_cast<double>(ref),
                static_cast<double>(roundedRef),
                static_cast<double>(std::fabs(got - ref)),
                std::isfinite(got) && got == roundedRef ? "YES" : "NO");
        }
    }

    bool ok = true;                                      // 汇总窗口、数值和 guard 的检查结果。
    bool windowsOK = true;                               // 单独记录窗口连续性和范围是否合法。
    uint64_t next = 0;                                   // 记录下一个核预期的起点，单位是元素。
    uint64_t sum = 0;                                    // 累加所有核的有效长度，单位是元素。
    uint32_t badCount = 0;                               // 超出容差的元素个数。
    float maxErr = 0.0f;                                 // 观测到的最大绝对误差。
    uint32_t worstIdx = 0;                               // 最大误差对应的元素下标，便于定位。
    for (uint32_t core = 0; core < blockDim; ++core) {      // 逐核检查预期窗口；数值检查在全局扫描中做。
        const CoreRange r = ExpectedRange(core, blockDim, totalLength); // 复用已推演的分配公式。
        if (r.offset != next) {                          // 核窗口应按编号连续衔接，空窗口也允许。
            std::printf("[CORE_%u] FAIL offset=%u expected=%llu\n", core, r.offset,
                        static_cast<unsigned long long>(next));
            ok = false;
            windowsOK = false;
        }
        next = static_cast<uint64_t>(r.offset) + r.length; // 更新为当前核窗口的末端。
        sum += r.length;                                 // 将本核有效长度加入覆盖总量。
        if (r.offset > totalLength || r.length > totalLength - r.offset) { // 防止错误窗口导致越界读。
            std::printf("[CORE_%u] FAIL window out of bounds\n", core);
            ok = false;
            windowsOK = false;
        }
        std::printf("[CORE_%u] SUCCESS offset=%u length=%u\n", core, r.offset, r.length); // 空核为 0。
    }
    const bool coverageOK = windowsOK && sum == totalLength && next == totalLength; // 连续性、总量和末端均须通过。
    if (!coverageOK) ok = false;
    std::printf("[COVERAGE] %s sum=%llu next=%llu expected=%u\n",
                coverageOK ? "PASS" : "FAIL", static_cast<unsigned long long>(sum),
                static_cast<unsigned long long>(next), totalLength);

    // 数值比对：主机侧 tanhf 作 golden，逐元素检查 |y - ref| <= 1e-3。
    for (uint32_t i = 0; i < totalLength; ++i) {
        const float ref = std::tanh(static_cast<float>(x[i])); // FP16 输入先升精度再求参考值。
        const float got = static_cast<float>(y[i]);       // 输出按 FP16 语义读回。
        const float err = std::fabs(got - ref);           // 绝对误差，题面约定用绝对量判定。
        if (err > maxErr) {                               // 记录最大误差及位置，失败时直接可查。
            maxErr = err;
            worstIdx = i;
        }
        if (err > TOLERANCE) ++badCount;                  // 超出容差的元素只计数，避免刷屏。
    }
    if (badCount != 0) ok = false;                        // 任一元素超差都判失败。
    std::printf("[NUMERIC] %s bad=%u/%u max_err=%.6g at %u (got=%.6g ref=%.6g)\n",
                badCount == 0 ? "PASS" : "FAIL", badCount, totalLength,
                static_cast<double>(maxErr), worstIdx,
                static_cast<double>(static_cast<float>(y[worstIdx])),
                static_cast<double>(std::tanh(static_cast<float>(x[worstIdx]))));

    const uint8_t* px = reinterpret_cast<const uint8_t*>(x); // guard 区按原始字节比对：0xCCCC 这类
    const uint8_t* py = reinterpret_cast<const uint8_t*>(y); // 重复字节模式按 half 读是 NaN，不能作比较值。
    size_t guardBad = 0;                                  // 统计输出尾部 guard 被改写的字节数。
    size_t xGuardBad = 0;                                 // 输入 guard 不应被 kernel 触碰。
    for (size_t j = logicalBytes; j < allocatedBytes; ++j) { // guard 区间独立于逻辑区比对。
        if (py[j] != OUTPUT_SENTINEL) ++guardBad;          // 输出 guard 被改写 = 写回越界。
        if (px[j] != 0xCC) ++xGuardBad;                    // 输入 guard 被改写同样是越界信号。
    }
    if (guardBad != 0 || xGuardBad != 0) ok = false;       // 任一 guard 失守都判失败。
    std::printf("[GUARD] %s x_guard_bad=%zu y_guard_bad=%zu\n",
                (guardBad == 0 && xGuardBad == 0) ? "PASS" : "FAIL", xGuardBad, guardBad);

    std::printf("tanh numeric: %s\n", ok ? "PASS" : "FAIL"); // 最终判定行；矩阵脚本以退出码为准。

    AscendC::GmFree(x);                                   // 释放模拟 GM 输入。
    AscendC::GmFree(y);                                   // 释放模拟 GM 输出。
    return ok ? 0 : 1;
}

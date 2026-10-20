#include "kernel_operator.h"
#include "tanh_custom_tiling.h"

constexpr int32_t BUFFER_NUM = 2; // tensor num for each queue
constexpr uint32_t BLOCK_BYTES = 32; // MTE 一次搬运的最小单位（字节）
constexpr uint32_t ELEMS_PER_BLOCK = BLOCK_BYTES / sizeof(DTYPE_X); // 每块的元素数；tileLength 必须是它的整数倍

static_assert(BLOCK_BYTES % sizeof(DTYPE_X) == 0, "element straddles 32B block boundary");
static_assert(sizeof(DTYPE_X) == sizeof(DTYPE_Y), "DTYPE_X and DTYPE_Y must have the same size");


__aicore__ inline uint32_t DivCeil32(uint32_t a, uint32_t b) {
    return a/b + static_cast<uint32_t>(a % b != 0);
}
__aicore__ inline uint32_t AlignUp32(uint32_t value, uint32_t alignment)
{
    return DivCeil32(value, alignment) * alignment;
}

__aicore__ inline uint32_t MinU32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}
class KernelTanh {
public:
    __aicore__ inline KernelTanh() {}
    __aicore__ inline void Init(GM_ADDR x, GM_ADDR y, uint32_t totalLength, uint32_t tileNumIn)
    {
        const uint32_t blockNumRaw = static_cast<uint32_t>(AscendC::GetBlockNum()); // GetBlockNum 返回 int64_t
        const uint32_t blockDim = blockNumRaw == 0 ? 1u : blockNumRaw; // 防除零兜底
        const uint32_t idx = AscendC::GetBlockIdx();
     // 分配单位先用完整的 32B 块，保证各核起点按本方案对齐。
        const uint32_t chunks = totalLength / ELEMS_PER_BLOCK;
        const uint32_t tail = totalLength % ELEMS_PER_BLOCK;
        const uint32_t per = chunks / blockDim;
        const uint32_t rem = chunks % blockDim;
        this->coreOffset = (idx * per +  MinU32(idx,rem)) * ELEMS_PER_BLOCK;
        this->coreLength = (per + (idx < rem ? 1u : 0u))* ELEMS_PER_BLOCK +
                            (idx + 1 == blockDim ? tail : 0); 
        this->tileNum = tileNumIn == 0 ? 1u : tileNumIn;
        // 本实验的 tileNum 为 8（另允许 0 走兜底），host 需保证乘法不溢出。
        const uint32_t slots = this->tileNum * BUFFER_NUM;
        const uint32_t alignedLength = AlignUp32(DivCeil32(this->coreLength, slots), ELEMS_PER_BLOCK);
        this->tileLength = alignedLength < ELEMS_PER_BLOCK ? ELEMS_PER_BLOCK : alignedLength; // 空核时兜底为一个块

        if (this->coreLength == 0) {
            return;
        }
        // coreOffset 只在这里加一次。后续 xGm[offset] 的 offset 是核内偏移。
        xGm.SetGlobalBuffer((__gm__ DTYPE_X*)x + this->coreOffset,
                            this->coreLength);
        yGm.SetGlobalBuffer((__gm__ DTYPE_Y*)y + this->coreOffset,
                            this->coreLength);
        pipe.InitBuffer(inQueueX, BUFFER_NUM,
                        this->tileLength * sizeof(DTYPE_X));
        pipe.InitBuffer(outQueueY, BUFFER_NUM,
                        this->tileLength * sizeof(DTYPE_Y));


    }
    __aicore__ inline void Process()
    {
        // 如 total=100 时的 core6：窗口为空，没有数据需要搬入、计算或搬出。
        if (this->coreLength == 0) {
            return;
        }
        const uint32_t loopCount = DivCeil32(this->coreLength, this->tileLength);
        
        for (uint32_t i = 0; i < loopCount; ++i) {
            const uint32_t offset = i * this->tileLength;
            const uint32_t count = MinU32(
                this->tileLength, this->coreLength - offset);
            CopyIn(offset, count);
            Compute(count);
            CopyOut(offset, count);
        }
    }

private:
    __aicore__ inline void CopyIn(uint32_t offset, uint32_t count)
    {
         AscendC::LocalTensor<DTYPE_X> xLocal = inQueueX.AllocTensor<DTYPE_X>();
        const uint32_t validBytes = count * sizeof(DTYPE_X);
        if (validBytes % BLOCK_BYTES == 0) {
            // 这个连续 DataCopy 重载的 count 使用元素数。
            AscendC::DataCopy(xLocal, xGm[offset], count);
        } else {
            // 一个连续数据段；DataCopyExtParams::blockLen 使用字节数。
            AscendC::DataCopyExtParams copyParams{1, validBytes, 0, 0, 0};
            AscendC::DataCopyPadExtParams<DTYPE_X> padParams{};
            // isPad=false：不指定用户填充值，不代表 UB 内绝不会补齐。
            AscendC::DataCopyPad(xLocal, xGm[offset], copyParams, padParams);
    }
     inQueueX.EnQue(xLocal);
}

    __aicore__ inline void Compute(uint32_t count)
    {
        AscendC::LocalTensor<DTYPE_X> xLocal = inQueueX.DeQue<DTYPE_X>();
        AscendC::LocalTensor<DTYPE_Y> yLocal = outQueueY.AllocTensor<DTYPE_Y>();
        // 身份拷贝：本实验按原题 FP16 输入输出运行，尚未调用 Tanh API。
        // 只计算 count 个有效元素，不把 UB 补齐区域当成有效输入。
        AscendC::Muls(yLocal, xLocal, (DTYPE_Y)1.0f, count);
        outQueueY.EnQue<DTYPE_Y>(yLocal);
        inQueueX.FreeTensor(xLocal);
    }

    __aicore__ inline void CopyOut(uint32_t offset, uint32_t count)
    {
        AscendC::LocalTensor<DTYPE_Y> yLocal = outQueueY.DeQue<DTYPE_Y>();
        const uint32_t validBytes = count * sizeof(DTYPE_Y);
        if (validBytes % BLOCK_BYTES == 0) {
            AscendC::DataCopy(yGm[offset], yLocal, count);
        } else {
            AscendC::DataCopyExtParams copyParams{1, validBytes, 0, 0, 0};
            // UB -> GM 的重载没有 padParams，只回写 validBytes 个字节。
            AscendC::DataCopyPad(yGm[offset], yLocal, copyParams);
        }
        outQueueY.FreeTensor(yLocal);
    }


private:
    AscendC::TPipe pipe;
    AscendC::TQue<AscendC::QuePosition::VECIN, BUFFER_NUM> inQueueX;
    AscendC::TQue<AscendC::QuePosition::VECOUT, BUFFER_NUM> outQueueY;
    AscendC::TBuf<AscendC::QuePosition::VECCALC> tmpBuf0,tmpBuf1,tmpBuf2;
    AscendC::GlobalTensor<DTYPE_X> xGm;
    AscendC::GlobalTensor<DTYPE_Y> yGm;
    uint32_t coreOffset = 0;
    uint32_t coreLength = 0;
    uint32_t tileNum = 1;
    uint32_t tileLength = ELEMS_PER_BLOCK;
};

extern "C" __global__ __aicore__ void tanh_custom(GM_ADDR x, GM_ADDR y, GM_ADDR workspace, GM_ADDR tiling) {
    REGISTER_TILING_DEFAULT(TanhCustomTilingData);
    GET_TILING_DATA(tilingData, tiling);
    KernelTanh op;
    op.Init(x, y, tilingData.totalLength, tilingData.tileNum);
    op.Process();
}
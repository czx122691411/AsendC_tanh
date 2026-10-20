#include <cassert>
#include <cstdio>
#include <initializer_list>
struct CoreRange { unsigned offset; unsigned length; };

// TODO 你来写：按 16 元素块均分，前 rem 个核各多 1 块，tail 归最后一核
CoreRange ExpectedRange(unsigned idx, unsigned blockDim, unsigned total)
{
    assert(blockDim > 0);
    assert(idx < blockDim);
    //两个基本约束：EPB 16 元素为一块，blockDim 核数 = 8
    //在这个分配约束的基础上有整除部分，有余数部分
    //单位：per：基本块/核；rem：剩余块数；tail:无法满足为一块的元素数；chunks：可以分配的完整块数
    //分配粒度
    const unsigned EPB  = 16;  // fp16: 32B / 2B
    //完整块数（16 元素为一块）
    const unsigned chunks = total / EPB;
    //每核基本分配的完整块数
    const unsigned per = chunks / blockDim;
    //剩余完整块数
    const unsigned rem = chunks % blockDim;
    //不足16的尾数
    const unsigned tail = total % EPB;

    // 组装：offset 按“核序号 × 基本块数 + 前面多分块的核数”累乘块大小；
    // length 前 rem 个核各多 1 块，最后一核再加 tail。
    CoreRange r;
    r.offset = (idx * per + ((idx < rem) ? idx : rem)) * EPB;
    r.length = (per + ((idx < rem) ? 1u : 0u)) * EPB
             + ((idx + 1 == blockDim) ? tail : 0u);
    return r;
}

int main()
{
    int failures = 0;

    // 不变量检查：对一组形状自动验证（不用手算）
    for (unsigned total : {1u, 31u, 100u, 16380u, 16400u, 16384u}) {
        unsigned next = 0, sum = 0;
        for (unsigned i = 0; i < 8; ++i) {
            CoreRange r = ExpectedRange(i, 8, total);
            if (r.offset != next) {
                printf("FAIL total=%u core%u: offset=%u expected=%u\n",
                       total, i, r.offset, next);
                ++failures;
            }
            next = r.offset + r.length;
            sum += r.length;
        }
        if (sum != total || next != total) {
            printf("FAIL total=%u: sum=%u end=%u\n", total, sum, next);
            ++failures;
        }
    }

    // 抽查：填入纸上推演的 3 个答案（写错会 assert）
    CoreRange a = ExpectedRange(7, 8, 100);
    assert(a.offset == 96 && a.length == 4);      // core7
    CoreRange b = ExpectedRange(0, 8, 16400);
    assert(b.length == 2064);                     // core0
    CoreRange c = ExpectedRange(7, 8, 16380);
    assert(c.length == 2044);                     // core7

    if (failures != 0) {
        printf("split formula: FAIL (%d invariant violations)\n", failures);
        return 1;
    }
    printf("split formula: ALL PASS\n");
    return 0;
}

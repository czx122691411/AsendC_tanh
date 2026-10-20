#include <cstdint>
#include <cstdio>

// 模拟 kernel 头部的合同：每个 dtype 实例化时检查一次
template <typename T>
constexpr unsigned ElemsPerBlock()
{
    static_assert(32u % sizeof(T) == 0, "element straddles 32B block boundary");
    return 32u / sizeof(T);
}

struct Elem6 { char d[6]; };   // 假想的 6 字节 dtype

int main()
{
    printf("fp16: EPB=%u\n", ElemsPerBlock<uint16_t>());
    printf("fp32: EPB=%u\n", ElemsPerBlock<float>());
    printf("int8: EPB=%u\n", ElemsPerBlock<int8_t>());
    // 取消下面这行注释再编译——观察会发生什么
    //printf("elem6: EPB=%u\n", ElemsPerBlock<Elem6>());
}

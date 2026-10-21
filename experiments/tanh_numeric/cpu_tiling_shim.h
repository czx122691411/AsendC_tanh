/*
 * CPU 域调试的 Tiling 宏替代。
 *
 * 真机编译时 ccec 会自动生成 REGISTER_TILING_DEFAULT / GET_TILING_DATA；
 * 用 g++ 直接编译 kernel 时这两个宏不存在，需要在包含 kernel 之前提供。
 * 用法：g++ ... -include experiments/identity_copy/cpu_tiling_shim.h ...
 */
#ifndef IDENTITY_COPY_CPU_TILING_SHIM_H
#define IDENTITY_COPY_CPU_TILING_SHIM_H

#include <cstring>
#include "tanh_custom_tiling.h"

#define REGISTER_TILING_DEFAULT(T)

#define GET_TILING_DATA(tiling_data, tiling_arg)              \
    TanhCustomTilingData tiling_data;                         \
    std::memcpy(&tiling_data, (const void*)(tiling_arg),      \
                sizeof(TanhCustomTilingData))

#endif

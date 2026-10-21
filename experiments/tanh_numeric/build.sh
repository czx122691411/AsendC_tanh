#!/bin/bash
# 编译并在 CPU 域运行 Tanh 数值回归 harness（无 NPU 时的功能验证层级）。
# 编译选项与 identity_copy/build.sh 保持一致，仅目标与运行矩阵不同。
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
A=${ASCEND_HOME_PATH:-/home/developer/Ascend/cann-9.0.0}
OUT="$HERE/build"
mkdir -p "$OUT"

if [ ! -d "$A/asc" ]; then
    echo "CANN not found at $A; set ASCEND_HOME_PATH" >&2
    exit 1
fi

g++ -std=c++17 -g -O0 \
    -D_GLIBCXX_USE_CXX11_ABI=0 -DASCENDC_CPU_DEBUG=1 -DASCENDC_DEBUG=1 \
    -D__NPU_ARCH__=2201 -D__CCE_KT_TEST__=1 -D__CCE_AICORE__=220 -D__DAV_C220__ \
    -DDTYPE_X=half -DDTYPE_Y=half \
    -I "$ROOT/TanhCustom/op_kernel" \
    -include "$HERE/cpu_tiling_shim.h" \
    -isystem "$A/tools/tikicpulib/lib/include" \
    -isystem "$A/include" \
    -isystem "$A/asc" \
    -isystem "$A/asc/impl/adv_api" \
    -isystem "$A/asc/impl/basic_api" \
    -isystem "$A/asc/impl/c_api" \
    -isystem "$A/asc/impl/micro_api" \
    -isystem "$A/asc/impl/simt_api" \
    -isystem "$A/asc/impl/utils" \
    -isystem "$A/asc/include" \
    -isystem "$A/asc/include/adv_api" \
    -isystem "$A/asc/include/adv_api/matmul" \
    -isystem "$A/asc/include/aicpu_api" \
    -isystem "$A/asc/include/basic_api" \
    -isystem "$A/asc/include/c_api" \
    -isystem "$A/asc/include/interface" \
    -isystem "$A/asc/include/micro_api" \
    -isystem "$A/asc/include/simt_api" \
    -isystem "$A/asc/include/tiling" \
    -isystem "$A/asc/include/utils" \
    "$HERE/main.cpp" "$ROOT/TanhCustom/op_kernel/tanh_custom.cpp" \
    -o "$OUT/tanh_numeric" \
    -Wl,--disable-new-dtags \
    -Wl,-rpath,"$A/lib64" \
    -Wl,-rpath,"$A/tools/tikicpulib/lib" \
    -Wl,-rpath,"$A/tools/tikicpulib/lib/Ascend910B1" \
    -Wl,-rpath,"$A/tools/simulator/Ascend910B1/lib" \
    -L "$A/lib64" \
    -L "$A/tools/tikicpulib/lib" \
    -L "$A/tools/tikicpulib/lib/Ascend910B1" \
    -L "$A/tools/simulator/Ascend910B1/lib" \
    -Wl,-Bdynamic,--no-as-needed \
    -lascendc_acl_stub -lpem_davinci \
    -lcpudebug_cceprint -lcpudebug_npuchk -lcpudebug_stubreg -lcpudebug \
    -lc_sec -lstdc++ -lruntime -lregister -lerror_manager -lprofapi \
    -lge_common_base -lunified_dlog -lmmpa -ldl -lascend_dump

echo "built: $OUT/tanh_numeric"
# CPU 域运行会在当前目录生成 cceprint/、npuchk/、stub_reg.log，放进 build/ 以免污染仓库根目录。
cd "$OUT"
if [ "$#" -gt 0 ]; then exec ./tanh_numeric "$@"; fi
rc=0
for n in 16384 16400 16380 100 2050 31 1; do
    echo "=== totalLength=$n ==="
    ./tanh_numeric "$n" || rc=1
done
exit $rc

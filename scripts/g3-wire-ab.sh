#!/usr/bin/env bash
# G3 接线验证（zimg）：off/on 双配置交叉构建 + qemu 跑 resize 探针。
# 口径：两侧同 -march=rv64gcv，唯一变量是 --disable-simd（在 RISC-V 上就等于
# "有没有 RVV 内核"，因为上游没有任何其他 RISC-V SIMD）。
# 判据：CPU_NONE 与 CPU_AUTO 在两侧输出逐字节相同（定点卷积必须位精确）。
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE=${IMAGE:-rvv-cross:2}

docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc "
set -e
mkdir -p build/g3-ab/bin
for side in off on; do
  if [ \$side = off ]; then CONF='--disable-simd'; else CONF=''; fi
  echo \"=== 构建 \$side（configure: \${CONF:-默认启用 simd}）===\"
  HOST=riscv64-linux-gnu EXTRA_C_FLAGS='-march=rv64gcv' EXTRA_CONF=\"\$CONF\" \
    ./cmake/pkg-build.sh /work/src /work/build/g3-ab/\$side/pkg > /tmp/zimg-\$side.log 2>&1 \
    || { tail -40 /tmp/zimg-\$side.log; exit 1; }
  riscv64-linux-gnu-g++ -march=rv64gcv -O2 -static \
    -I build/g3-ab/\$side/pkg/prefix/include test/resize_bench.c \
    build/g3-ab/\$side/pkg/prefix/lib/libzimg.a -lm \
    -o build/g3-ab/bin/resize_\$side
  echo \"--- \$side CPU_NONE ---\"; qemu-riscv64 -L /usr/riscv64-linux-gnu build/g3-ab/bin/resize_\$side none 1
  echo \"--- \$side CPU_AUTO ---\"; qemu-riscv64 -L /usr/riscv64-linux-gnu build/g3-ab/bin/resize_\$side auto 1
done
" 2>&1 | tail -20

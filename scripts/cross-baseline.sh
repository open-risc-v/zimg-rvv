#!/usr/bin/env bash
# 交叉编译基线版本：在 rvv-cross 容器里把 baseline（上游原样）编成 riscv64，
# 探针用 qemu-user 跑，输出与 native 基线逐字段比对。
#
#   ./scripts/cross-baseline.sh                # 默认 -march=rv64gc（无向量，纯基线）
#   MARCH=rv64gcv ./scripts/cross-baseline.sh  # 上游自带 RVV 代码时，看向量路径
#
# 前置：docker 里有 rvv-cross:1 镜像（riscv64-linux-gnu-gcc + qemu-riscv64 +
# cmake/ninja/meson/autotools）。没有就先用 scripts/docker-cross.sh 建。
# 上游构建系统不是 CMake 时，只改 "编上游" 那一段（见 references/adapt-build-system.md §8）。
#
# 纪律：这里只编 baseline，不编 src，也不开 RVV_KERNELS —— 它回答"上游在 riscv64 上
# 能不能编、能不能跑、输出和 native 是否一致"，性能数字一律不从这里取。
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=${IMAGE:-rvv-cross:1}
MARCH=${MARCH:-rv64gc}
OUT=build/riscv64-b
HARNESS=${HARNESS:-pkg_harness}   # test/ 下探针的可执行名（target 名）

docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc "
set -e
rm -rf ${OUT}

# 1) 编上游到 ${OUT}/prefix
cmake -S baseline -B ${OUT}/bld \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/work/${OUT}/prefix \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \
  -DCMAKE_C_FLAGS='-march=${MARCH}' > /tmp/cfg.log 2>&1 || { tail -30 /tmp/cfg.log; exit 1; }
cmake --build ${OUT}/bld -j > /tmp/bld.log 2>&1 || { tail -20 /tmp/bld.log; exit 1; }
cmake --install ${OUT}/bld > /dev/null

# 2) 编探针（-DPKG_PREFIX 跳过 pkg 目标，直接用上面的前缀）
cmake -S . -B ${OUT}/harness -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \
  -DCMAKE_C_FLAGS='-march=${MARCH}' \
  -DPKG_PREFIX=/work/${OUT}/prefix > /tmp/h.log 2>&1 || { tail -30 /tmp/h.log; exit 1; }
cmake --build ${OUT}/harness -j > /tmp/hb.log 2>&1 || { tail -20 /tmp/hb.log; exit 1; }

# 3) 跑（qemu-user + 目标 sysroot）
cd /work && mkdir -p ${OUT}/out
qemu-riscv64 -L /usr/riscv64-linux-gnu ${OUT}/harness/${HARNESS} ${OUT}/out/out.bin > ${OUT}/out/hash.txt
echo \"--- riscv64（${MARCH}）---\"; cat ${OUT}/out/hash.txt
echo \"--- native 基线 ---\"; cat build/ab-data/out/01_*/a/hash.txt
"

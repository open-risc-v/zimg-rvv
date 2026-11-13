#!/usr/bin/env bash
# 构建上游 zimg（autotools）→ <out>/prefix：静态库 libzimg。
# zimg 用 autogen.sh 生成 configure（源码树里没有 configure），必须在副本里做，
# 不能在 baseline 里跑 autoreconf（会把 baseline 弄脏）。
set -euo pipefail
SRC=$1
OUT=$2
rm -rf "${OUT}"
mkdir -p "${OUT}/src"
cp -R "${SRC}" "${OUT}/src/pkg"
cd "${OUT}/src/pkg"
NPROC=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
./autogen.sh >/dev/null 2>&1
./configure --prefix="${OUT}/prefix" --disable-shared --enable-static \
  --disable-testapp --disable-example --disable-unit-test >/dev/null
make -j"${NPROC}" >/dev/null
make install >/dev/null
echo "== 上游 zimg 构建完成：${OUT}/prefix"

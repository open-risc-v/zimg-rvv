#!/usr/bin/env bash
# 本机不编译：native 基线同样在 Linux 容器里跑（同架构、同 glibc，结果可复现）。
# 用法：./scripts/native-baseline.sh [镜像]，默认 rvv-cross:2。
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE=${1:-${IMAGE:-rvv-cross:2}}
docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc '
  set -e
  rm -rf build/ab-a build/ab-b build/ab-data
  ./test/run_ab.sh
'

#!/usr/bin/env bash
# 本机不编译：native A/B 在 Linux 容器里跑。
set -euo pipefail
cd "$(dirname "$0")/.."
IMAGE=${1:-${IMAGE:-rvv-cross:2}}
docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc '
  set -e
  rm -rf build/ab-a build/ab-b build/ab-data
  ./test/run_ab.sh
'

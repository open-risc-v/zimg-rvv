#!/usr/bin/env bash
# 真实包用例：zimg 的 resize 与 YUV->RGB 两条路径，输出确定性二进制 + 哈希文本。
set -euo pipefail
: "${STAGE:=run}" "${SIDE:=A}" "${BIN:=build/native}" "${DATA:=build/ab-data}" "${OUT:=build/ab-data/out}"
mkdir -p "$DATA" "$OUT"
case "$STAGE" in
  gen) : > "$DATA/zimg-input.txt" ;;   # 输入在探针里用固定公式生成，无需外部语料
  run)
    "$BIN/zimg_harness" "$OUT/zimg.bin" > "$OUT/hash.txt"
    ;;
  *) echo "unknown STAGE=$STAGE" >&2; exit 2 ;;
esac

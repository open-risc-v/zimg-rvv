# 进度 —— `media-libs/zimg`

倒序追加，每次一个条目。门禁变更必须同时更新 `project.conf` 的 `GATE`，
否则主仓库看板会与事实脱节。

## 2026-11-13 · G0 planned

- 生成骨架（`rvv-port-project` Skill，模板来自 `libspeex-rvv`）
- 范围与通过标准写入 `docs/plan.md`
- 上游 URL：https://github.com/sekrit-twc/zimg；固定点：`67e0603271c080e22c8429856dd4a8a56587e61e`
- 下一步（G1）：
  1. `./scripts/fetch-upstream.sh --dry-run` 确认 URL/SHA 后再拉取
  2. `src/` 初始化为 upstream 的副本（一次性提交，便于 diff）
  3. `./scripts/build.sh riscv64` + `./test/run_ab.sh` 跑通

## 2026-11-16 G3：垂直 resize RVV 内核（整链 2.02×，本轮最高）

- 新文件 `src/zimg/resize/riscv/resize_impl_riscv.{h,cpp}`，1.14 定点 i16 系数，
  `vwmacc_vx_i32m2` 加宽累加，unpack 用 u16 减法回绕 + reinterpret i16。
- 构建系统改动：configure.ac 加 riscv64 host 分支 + `ZIMG_RVV` + `AM_CONDITIONAL(RVVSIMD)`；
  Makefile.am `if RVVSIMD`；cpuinfo.h 加 `CPUClass::RISCV_RVV`；resize_impl.cpp 分派。
- 真机内核级（纯垂直 1920x1080→1920x720）：23.54–23.57 ms → **3.096–3.103 ms = 7.6×**。
- 真机整链（1920x1080→1280x720）：C 41.0–41.4 ms → **20.3–20.4 ms = 2.02×**，5/5 同向。
- 水平 resize 未向量化（25.7 ms 原样保留），是整链剩下的另一半。
- 坑：第一版忽略 cpu 参数，`CPU_NONE` 也跑 RVV，A/B 口径失真（详见证据文档 §6）。

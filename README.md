# zimg-rvv

`media-libs/zimg` 的 RISC-V RVV 1.0 移植工程。骨架由 `rvv-port-project` Skill 生成，
范式与 `libspeex-rvv` 一致：**A/B 双树 + 内核差分 + paired 基准**，先建门禁，再写内核。

| 项 | 值 |
|---|---|
| Gentoo 包 | `media-libs/zimg` |
| 上游 | https://github.com/sekrit-twc/zimg |
| 上游固定点 | `67e0603271c080e22c8429856dd4a8a56587e61e` |
| 许可证 | WTFPL |
| 难度 / 路径 | D3 / 路径 B |
| 优先级 / 波次 | P1 / W3 |
| 验证口径 | 容差 |
| 主仓库编号 | {{RVV_ID}} |
| 门禁状态 | G0 planned |

## 目录

```
.
├── baseline/            上游只读快照（fetch-upstream.sh 拉取，勿手改）
├── src/                 移植工作副本（RVV 内核放 src/<lib>/rvv/）
├── test/
│   ├── run_ab.sh        A/B 逐字节比对（门禁）
│   ├── cases/           每个用例一个 .sh：生成输入 → 跑 $BIN → 产出写 $OUT
│   ├── diff_kernels.c   内核差分（add_test 注册到 ctest）
│   └── cpu_probe.c      VLEN / 扩展 / 微基准（写进 docs/plan.md）
├── benchmark/
│   ├── pair_bench.sh    paired 基准（A/B 交替 N 次取中位）
│   └── cases/
├── cmake/               工具链 + 上游构建适配（upstream.cmake）
├── scripts/             build / docker-cross / device-test / fetch-upstream
├── docs/                plan / rvv-notes / perf-log / progress / final-report
└── project.conf         元数据（主仓库看板的数据源）
```

## 起手三步

```bash
./scripts/fetch-upstream.sh          # 只拉上游到 baseline/，确认 URL/SHA 后才执行
./scripts/build.sh riscv64           # 交叉构建工具，验证工具链
./test/run_ab.sh                     # 先跑空用例，确认 A/B 框架可用（G1）
```

**顺序纪律**：G2（基线数字）之前不要写内核；没有 A/B 通过的内核不算完成。
规则见 `docs/plan.md` 与 `skills/rvv-kernel-loop/SKILL.md`。

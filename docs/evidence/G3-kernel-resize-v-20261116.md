# zimg G3：垂直 resize 定点卷积的 RVV 内核

- 日期：2026-11-16
- 上游：`sekrit-twc/zimg` @ `67e0603271c080e22c8429856dd4a8a56587e61e`
- 构建：autotools 静态；两侧同 `-march=rv64gcv`，唯一变量 `--disable-simd`
- 目标板：SpacemiT K3 / <distro>，`Linux k3 6.18.3 riscv64`，RVV 1.0

## 1. 为什么选垂直方向

zimg 的 resize 分水平/垂直两遍，都是 `filter_width` 个 tap 的加权和：

- 水平（`resize_line_h_u16_c`）：输出连续、输入是 `left[j]` 索引的 gather，向量化要处理
  per-output-pixel 不同起点，收益受限于 gather。
- **垂直（`resize_line_v_u16_c`）：输出列连续、每个输出行只换一组系数**，是标准的
  "多行按列加权求和"，向量化维度是列，RVV 上结构最干净。

而且垂直是整条 resize 的主体：bicubic 每输出像素要读 4 个源行。所以本轮只做垂直，水平留 C。

## 2. 接线（要改上游构建系统，这是本次最大的形态成本）

zimg 的 SIMD 分支是 configure 时按 `host_cpu` 写死的（`ZIMG_X86` / `ZIMG_ARM`），
RISC-V 上原本一条分支都没有。改动：

1. `configure.ac`：`host_cpu` 的 `AS_CASE` 加 `[riscv64], [RISCV="yes"]`，
   `AC_DEFINE([ZIMG_RVV])`，并加 `AM_CONDITIONAL([RVVSIMD])`。
2. `Makefile.am`：`if RVVSIMD` 把 `src/zimg/resize/riscv/*` 加进
   `libzimg_internal_la_SOURCES`（不需要独立 `.la`，RVV 指令就在 `-march` 里）。
3. `common/cpuinfo.h`：`CPUClass` 加 `RISCV_RVV` 一个取值（`#elif defined(ZIMG_RVV)`）。
4. `resize/resize_impl.cpp`：`#elif defined(ZIMG_RVV)` 分支，只建垂直实现。

A/B 口径：`--disable-simd` 在原版语义上是"关掉 SIMD"，在 RISC-V 上就等于"没有 RVV 内核"
（上游没有别的 RISC-V SIMD），所以两侧同 `-march=rv64gcv`、唯一变量是 simd 开关。

## 3. 内核

`src/zimg/resize/riscv/resize_impl_riscv.cpp`，匹配 `resize_line_v_u16_c` 的 1.14 定点语义：

- **unpack**：C 版把 u16 转成 `x + INT16_MIN`。这里用 `vsub_vx u16 - 32768`（按 2^16 回绕）
  再 `vreinterpret u16m1 → i16m1` —— 比走 `vsext` 到 i32 再减便宜一半的位宽。
- **累加**：`vwmacc_vx_i32m2(acc, coeff_i16, xs_i16m1, vl)`：i16 系数标量广播 × i16 像素，
  加宽累加进 i32m2。系数是每行一组、在内层 tap 循环里取标量，不需要额外装载。
- **pack**：`acc + 2^13` → `vsra 14` → `+32768` → `vmax 0` → `vmin pixel_max` →
  `vnclipu_wx_u16m1` 收窄。逐位复刻 C 的舍入与截断，所以输出必须完全一致。
- vl 由 `vsetvl_e16m1(right - j)` 取，不假设 VLEN/MVL。

## 4. 功能验证（qemu，逐字节）

`./scripts/g3-wire-ab.sh`：off/on 两侧、`CPU_NONE`/`CPU_AUTO` 四种组合都输出
`fnv=3f41cccccde96e72`（1920×1080→1280×720 GRAY16 bicubic）。定点卷积位精确 → 一致。

## 5. 真机性能（K3）

### 5.1 内核级（纯垂直：1920×1080 → 1920×720，宽不变，只有垂直 filter）

| 侧 | 时间 | 哈希 |
|---|---|---|
| C（CPU_NONE） | 23.534–23.573 ms | `8ae2d305d97f41c5` |
| RVV（CPU_AUTO） | 3.096–3.103 ms | `8ae2d305d97f41c5` |

**7.57–7.62×**。同尺寸下纯水平（1920×1080→1280×1080）两侧都是 25.69–25.76 ms ——
水平未向量化，正好作为"收益全部来自垂直"的对照。

### 5.2 整链（1920×1080 → 1280×720，水平+垂直）

| 侧 | 时间 | 哈希 |
|---|---|---|
| off（C 构建，NONE/AUTO 同） | 40.959–41.454 ms | `3f41cccccde96e72` |
| on + CPU_NONE | 40.933–41.454 ms | `3f41cccccde96e72` |
| on + CPU_AUTO | **20.345–20.401 ms** | `3f41cccccde96e72` |

**约 2.01–2.03×**，5 轮 5/5 同向。归因：垂直那遍 23.5 → 3.1 ms（省 20.4 ms），
水平 25.7 ms 原地不动；整链 41.0 → 20.3 ms 与"只砍掉垂直那遍"的算术完全对得上。

## 6. 踩到的坑

1. **忽略 cpu 参数会让基准失真**：第一版 `create()` 只要不是水平方向就直接建 RVV 实现，
   于是 `ZIMG_CPU_NONE`（约定是"可移植 C 路径"）也在跑 RVV，真机上出现
   `on-none == on-auto == 20.3 ms`、而 `off ~41 ms` 的怪现象。修正后
   `CPU_NONE` 回落到 C，才是干净的三方对照：C 41 ms / RVV 20.3 ms。
   **教训：A/B 里"关掉"的那一侧必须真的关掉，CPU 参数属于口径而不是提示。**
2. `vnclipu_wx_u16m1` 的源必须是 `vuint32m2_t`，传 `vint32m2_t` 在 GCC 13 报类型错
   （先 `vreinterpret_v_i32m2_u32m2`）。
3. `m_pixel_max{ (1UL << depth) - 1 }` 在 C++ 花括号初始化里触发 `-Wnarrowing`（=error），
   要 `static_cast<uint32_t>`。
4. 上游 `--disable-simd` 那一侧仍然会自动向量化 C 代码（因为 `-march=rv64gcv` 在），
   所以 C 侧不是"纯标量"，这是本次 A/B 的正确基线（反过来说，也不能拿它跟
   `-march=rv64gc` 的标量版本比较）。
5. qemu 计时完全不可信：同一二进制 `none` 32.9 ms vs `auto` 47.1 ms，噪声比信号大。

## 7. 尚未做

- **水平 resize 的 RVV 版**（下一个最有价值的目标，25.7 ms 目前是整链另一半）。
- FLOAT 路径（`resize_line_v_f32_c` / `resize_line_h_f32_c`），本包只做 WORD。
- `depth` 模块（depth_convert / dither / error_diffusion）与 colorspace 的 RVV 版。
- 运行时 CPU 探测（现在等价于编译期开关 + cpu_type）。

## 8. 复现

- 功能：`./scripts/g3-wire-ab.sh`
- 整链/内核：`/root/rvv-k/zimg/resize_{off,on} <none|auto> <reps> [sw sh dw dh]`
  - 内核级用 `1920 1080 1920 720`（宽不变 → 只有垂直）
  - 水平对照用 `1920 1080 1280 1080`

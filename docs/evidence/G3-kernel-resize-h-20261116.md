# zimg 水平 resize 的 RVV 内核（G3 收口：整链 4.24×）

- 日期：2026-11-16
- 上游：`sekrit-twc/zimg` @ `67e0603271c080e22c8429856dd4a8a56587e61e`
- 构建：autotools 静态，两侧同 `-march=rv64gcv`，唯一变量 `--disable-simd`
- 目标板：SpacemiT K3 / Bianbu，`Linux k3 6.18.3 riscv64`，RVV 1.0

## 1. 为什么水平比垂直难

| | 垂直（`resize_line_v_u16_c`） | 水平（`resize_line_h_u16_c`） |
|---|---|---|
| 向量化维度 | 列 | 输出列 |
| 源数据 | 每个 tap 是一整行，**按列连续** `vle16` | 每个 tap 是 `src[left[j] + k]`，**起点随 j 变化** |
| 系数 | 每行一组，标量 `vwmacc_vx` | 每个输出列 (j,k) 各一个，要跨步装载成向量 |
| 结论 | 天然适合 | 必须 gather，或像 x86 那样预排 permute 表 |

上游 x86 AVX2 的做法是 `resize_line_h_perm_u16_avx2`：把 8 个输出的 tap 窗口
（`src[left..left+24]`）预排成 permute mask，每个 tap 用 `vpermq` 选出 8 个像素，
再做 `vpmaddwd`。这需要额外的 `permute_left/permute_mask` 预处理表。

RVV 有原生索引装载（`vluxei32`），所以不做预处理表，直接按像素 gather。

## 2. 实现（`resize_line_h_u16_rvv`）

一次处理 `vl` 个输出列（`vsetvl_e16m1`，VLEN=256 时 16 个）：

1. `leftv = vle32(filter.left.data() + j)`，再 `vsll 1` 转成**字节偏移**；
   注意 `u16m1` 与 `u32m2` 的 lane 数相同（都是 VLEN/16），所以一个 `vl` 通吃两种 SEW。
2. 每个 tap k：
   - `idx = leftv + 2k`，`vminu` 夹到 `(input_width-1)*2`（边界安全，语义不变）；
   - `x = vluxei32_u16(src, idx)` —— EEW=32 的索引，避开 u16 索引只能表 32K 像素的上限；
   - `x - 32768` 后 `vreinterpret u16m1 → i16m1`（与垂直同一套 unpack）；
   - 系数用 `vlse16` 跨步取 `data_i16 + j*stride_i16 + k`，步长 `stride_i16*2` 字节
     （**stride 不等于 filter_width**，不能按连续向量读）；
   - `vwmacc_vv_i32m2(acc, c, xs)` 加宽累加。
3. pack：与垂直逐位相同的 `+2^13 → vsra 14 → +32768 → vmax 0 → vmin pixel_max → vnclipu`。

关键点：**gather 一次取 vl 个不同起点**，这正是水平方向的本质；
把它写成"每个 tap 连续读"是错的（`left[j]` 每列都不同）。

## 3. 功能验证（qemu，逐字节）

`./scripts/g3-wire-ab.sh`，off/on × CPU_NONE/CPU_AUTO 四组合，1920×1080→1280×720 全部
`fnv=3f41cccccde96e72`。水平/垂直/整链三种尺寸的哈希也都一致（见下）。

## 4. 真机性能（K3）

### 4.1 内核级

| 场景 | C（CPU_NONE） | RVV（CPU_AUTO） | 比值 | 哈希 |
|---|---|---|---|---|
| 纯水平 1920×1080→1280×1080 | 25.689–25.880 ms | **9.606–9.643 ms** | **2.67×** | `fdbfc49578568fcf` |
| 纯垂直 1920×1080→1920×720 | 23.534–23.573 ms | **3.096–3.103 ms** | **7.57×** | `8ae2d305d97f41c5` |

### 4.2 整链（两遍都向量化）

| 场景 | C | RVV | 比值 | 哈希 |
|---|---|---|---|---|
| 1920×1080→1280×720（下采样） | 40.861–41.113 ms | **9.639–9.653 ms** | **4.24×** | `3f41cccccde96e72` |
| 1280×720→1920×1080（上采样） | 45.908 ms | **10.421 ms** | **4.41×** | `cb9a5e709d715894` |
| 640×480→1280×720（小尺寸） | 20.168 ms | **4.565 ms** | **4.42×** | `fa4a9830ee399d9c` |
| 1280×720→640×720（只横向） | 10.505 ms | **4.159 ms** | **2.53×** | `a54a177550e00b32` |

真机 5 轮 × 每侧 3 次取最优，`on-auto` 5/5 稳定在 9.639–9.653 ms。

**从 2.02× 提到 4.24×**：上一轮只做垂直时整链 41.0 → 20.3 ms；水平补上后 20.3 → 9.64 ms。
归因闭合：垂直那遍在新宽度（1280）下约 2.06 ms，水平约 7.6 ms，合计 9.6 ms。

## 5. 坑

1. **qemu 的 gather 慢得离谱**：同一份代码 qemu 下 245.6 ms vs C 32.5 ms（7.5 倍**变慢**），
   真机却是 9.64 ms（4.24 倍变快）。索引装载在 qemu 里是逐字节模拟的，**qemu 只能判功能**，
   这条再次验证：拿 qemu 计时做决策会把一个正确实现直接否掉。
2. `vluxei16` 的索引是 u16、单位是**字节**，只能表达到 32K 像素；宽图会静默回绕。
   改用 `vluxei32`（索引 u32m2、EEW=16 的数据）即可，lane 数不变。
3. 系数必须 `vlse16` 跨步取：`filter.data_i16` 的行距是 `stride_i16`，
   而 `stride_i16 != filter_width`（对齐填充），按连续向量读会取到下一行的数据。
4. `u32m2` 与 `u16m1` 的 lane 数都是 VLEN/16，所以一个 `vl` 可以同时驱动两者；
   但别把两者都当成 `vl` 去 `vsetvl`，会得到不同的 vl。

## 6. 尚未做

- FLOAT 路径（`resize_line_h_f32_c` / `resize_line_v_f32_c`）—— 目前 WORD 全覆盖。
- `depth`（depth_convert / dither / error diffusion）与 `colorspace` 模块。
- 运行时 CPU 探测（现在是编译期开关 + cpu_type 参数）。
- x86 那种"预排 permute 表 + 窗口内重排"的写法在 RVV 上是否能超过 gather，未验证。

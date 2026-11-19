#ifdef ZIMG_RVV

#include <algorithm>
#include <cstdint>
#include <memory>
#include <riscv_vector.h>
#include "common/pixel.h"
#include "graphengine/filter.h"
#include "resize/filter.h"
#include "resize/resize_impl.h"
#include "resize_impl_riscv.h"

namespace zimg::resize {

namespace {

/* 与 resize_impl.cpp 里的 unpack_pixel_u16 / pack_pixel_u16 逐位一致：
 * 输入 u16 先减 32768 视作有符号值参与 1.14 定点卷积，输出 (acc + 2^13) >> 14
 * 再加回 32768 并 clip。判据是"两侧逐字节相同"，所以这里不能顺手改成更快的
 * 舍入方式。 */
void resize_line_v_u16_rvv(const FilterContext &filter, const graphengine::BufferDescriptor &src,
                           const graphengine::BufferDescriptor &dst, unsigned i, unsigned left, unsigned right,
                           unsigned pixel_max)
{
	const int16_t *filter_coeffs = &filter.data_i16[i * filter.stride_i16];
	unsigned top = filter.left[i];
	const uint16_t *dst_line = dst.get_line<uint16_t>(i);

	unsigned j = left;
	while (j < right) {
		size_t vl = __riscv_vsetvl_e16m1(right - j);
		vint32m2_t accum = __riscv_vmv_v_x_i32m2(0, vl);

		for (unsigned k = 0; k < filter.filter_width; ++k) {
			const uint16_t *src_line = src.get_line<uint16_t>(top + k) + j;

			vuint16m1_t x = __riscv_vle16_v_u16m1(src_line, vl);
			vuint16m1_t y = __riscv_vsub_vx_u16m1(x, 32768u, vl);
			/* u16 - 32768 按 2^16 回绕，重新解释成 i16 就是 x + INT16_MIN */
			vint16m1_t xs = __riscv_vreinterpret_v_u16m1_i16m1(y);

			/* i16 系数（标量）× i16 像素（vint16m1）加宽累加到 i32m2 */
			accum = __riscv_vwmacc_vx_i32m2(accum, filter_coeffs[k], xs, vl);
		}

		{
			vint32m2_t t = __riscv_vadd_vx_i32m2(accum, 1 << 13, vl);
			t = __riscv_vsra_vx_i32m2(t, 14, vl);
			t = __riscv_vadd_vx_i32m2(t, 32768, vl);
			t = __riscv_vmax_vx_i32m2(t, 0, vl);
			t = __riscv_vmin_vx_i32m2(t, static_cast<int32_t>(pixel_max), vl);
			/* vnclipu 的源必须是 u32m2（GCC 13/14 不接受 i32m2） */
			vuint32m2_t u = __riscv_vreinterpret_v_i32m2_u32m2(t);
			vuint16m1_t o = __riscv_vnclipu_wx_u16m1(u, 0, __RISCV_VXRM_RNU, vl);
			__riscv_vse16_v_u16m1(const_cast<uint16_t *>(dst_line) + j, o, vl);
		}

		j += vl;
	}
}


/* 水平方向：每个输出列 j 用 filter.left[j] 起的 filter_width 个 tap。
 * 与垂直最大的不同是源起点随输出列变化，不能按列连续读，必须按像素 gather。
 * 做法：一次处理 vl 个输出列，把 filter.left[j] 装载成 u32 向量并转成字节偏移，
 * 每个 tap 用 vluxei32 取 vl 个像素（EEW=32 索引，避免 u16 索引 32K 像素的偏移上限），
 * 系数按 stride_i16 跨步装载，vwmacc 加宽累加。 */
void resize_line_h_u16_rvv(const FilterContext &filter, const uint16_t *src, uint16_t *dst,
                           unsigned left, unsigned right, unsigned pixel_max, unsigned input_width)
{
	unsigned j = left;
	while (j < right) {
		size_t vl = __riscv_vsetvl_e16m1(right - j);

		vuint32m2_t leftv = __riscv_vle32_v_u32m2(filter.left.data() + j, vl);
		leftv = __riscv_vsll_vx_u32m2(leftv, 1, vl);                       /* 像素索引 → 字节偏移 */
		vuint32m2_t cap = __riscv_vmv_v_x_u32m2((input_width - 1u) * 2u, vl);
		vint32m2_t accum = __riscv_vmv_v_x_i32m2(0, vl);

		for (unsigned k = 0; k < filter.filter_width; ++k) {
			vuint32m2_t idx = __riscv_vadd_vx_u32m2(leftv, k * 2, vl);
			idx = __riscv_vminu_vv_u32m2(idx, cap, vl);

			vuint16m1_t x = __riscv_vluxei32_v_u16m1(src, idx, vl);
			vuint16m1_t y = __riscv_vsub_vx_u16m1(x, 32768u, vl);
			vint16m1_t xs = __riscv_vreinterpret_v_u16m1_i16m1(y);

			const int16_t *cptr = filter.data_i16.data() + static_cast<size_t>(j) * filter.stride_i16 + k;
			vint16m1_t c = __riscv_vlse16_v_i16m1(cptr, static_cast<ptrdiff_t>(filter.stride_i16) * 2, vl);
			accum = __riscv_vwmacc_vv_i32m2(accum, c, xs, vl);
		}

		{
			vint32m2_t t = __riscv_vadd_vx_i32m2(accum, 1 << 13, vl);
			t = __riscv_vsra_vx_i32m2(t, 14, vl);
			t = __riscv_vadd_vx_i32m2(t, 32768, vl);
			t = __riscv_vmax_vx_i32m2(t, 0, vl);
			t = __riscv_vmin_vx_i32m2(t, static_cast<int32_t>(pixel_max), vl);
			vuint32m2_t u = __riscv_vreinterpret_v_i32m2_u32m2(t);
			vuint16m1_t o = __riscv_vnclipu_wx_u16m1(u, 0, __RISCV_VXRM_RNU, vl);
			__riscv_vse16_v_u16m1(const_cast<uint16_t *>(dst) + j, o, vl);
		}

		j += vl;
	}
}

class ResizeImplH_RVV : public ResizeImplH {
	uint32_t m_pixel_max;
public:
	ResizeImplH_RVV(const FilterContext &filter, unsigned height, unsigned depth) :
		ResizeImplH(filter, height, PixelType::WORD),
		m_pixel_max{ static_cast<uint32_t>((1UL << depth) - 1) }
	{}

	void process(const graphengine::BufferDescriptor *in, const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *, void *) const noexcept override
	{
		resize_line_h_u16_rvv(m_filter, in->get_line<uint16_t>(i), out->get_line<uint16_t>(i),
		                      left, right, m_pixel_max, m_filter.input_width);
	}
};

class ResizeImplV_RVV : public ResizeImplV {
	uint32_t m_pixel_max;
public:
	ResizeImplV_RVV(const FilterContext &filter, unsigned width, unsigned depth) :
		ResizeImplV(filter, width, PixelType::WORD),
		m_pixel_max{ static_cast<uint32_t>((1UL << depth) - 1) }
	{}

	void process(const graphengine::BufferDescriptor *in, const graphengine::BufferDescriptor *out,
	             unsigned i, unsigned left, unsigned right, void *, void *) const noexcept override
	{
		resize_line_v_u16_rvv(m_filter, *in, *out, i, left, right, m_pixel_max);
	}
};

} // namespace


std::unique_ptr<graphengine::Filter> create_resize_impl_v_riscv(const FilterContext &context, unsigned width, PixelType type, unsigned depth)
{
	if (type != PixelType::WORD)
		return nullptr;

	return std::make_unique<ResizeImplV_RVV>(context, width, depth);
}

std::unique_ptr<graphengine::Filter> create_resize_impl_h_riscv(const FilterContext &context, unsigned height, PixelType type, unsigned depth)
{
	if (type != PixelType::WORD)
		return nullptr;

	return std::make_unique<ResizeImplH_RVV>(context, height, depth);
}

} // namespace zimg::resize

#endif // ZIMG_RVV

#pragma once

#ifdef ZIMG_RVV

#ifndef ZIMG_RESIZE_RISCV_RESIZE_IMPL_RISCV_H_
#define ZIMG_RESIZE_RISCV_RESIZE_IMPL_RISCV_H_

#include <memory>

namespace graphengine {
class Filter;
}

namespace zimg {
enum class PixelType;
}

namespace zimg::resize {

struct FilterContext;

/* 垂直方向：输出列连续、每行一组系数，vwmacc 加宽累加（收益最高）。
 * 水平方向：起点随输出列变化，逐像素 gather + 跨步取系数。 */
std::unique_ptr<graphengine::Filter> create_resize_impl_v_riscv(const FilterContext &context, unsigned width, PixelType type, unsigned depth);
std::unique_ptr<graphengine::Filter> create_resize_impl_h_riscv(const FilterContext &context, unsigned height, PixelType type, unsigned depth);

} // namespace zimg::resize

#endif // ZIMG_RESIZE_RISCV_RESIZE_IMPL_RISCV_H_

#endif // ZIMG_RVV

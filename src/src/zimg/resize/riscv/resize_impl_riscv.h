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

/* 只做垂直方向（列方向连续，向量化收益最高）。水平方向暂无 RVV 实现，
 * 返回 nullptr 由调用方回落到 C。 */
std::unique_ptr<graphengine::Filter> create_resize_impl_v_riscv(const FilterContext &context, unsigned width, PixelType type, unsigned depth);

} // namespace zimg::resize

#endif // ZIMG_RESIZE_RISCV_RESIZE_IMPL_RISCV_H_

#endif // ZIMG_RVV

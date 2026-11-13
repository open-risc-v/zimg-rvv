# 上游接入：zimg（autotools）→ <build>/pkg/prefix，探针链接 libzimg 静态库。

set(PKG_ROOT "${CMAKE_BINARY_DIR}/pkg")
if(NOT DEFINED PKG_PREFIX)
  set(PKG_PREFIX "${PKG_ROOT}/prefix")
  add_custom_target(pkg ALL
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pkg-build.sh" "${SRC_DIR}" "${PKG_ROOT}"
    COMMENT "构建上游 zimg（${SRC_DIR}）"
    VERBATIM)
  set(HAVE_PKG_TARGET TRUE)
endif()

add_executable(zimg_harness test/pkg_zimg.c)
if(HAVE_PKG_TARGET)
  add_dependencies(zimg_harness pkg)
endif()
target_include_directories(zimg_harness PRIVATE "${PKG_PREFIX}/include")
target_link_directories(zimg_harness PRIVATE "${PKG_PREFIX}/lib")
# libzimg 是 C++，探针是 C：用 C++ 驱动链接（会带上 libstdc++ 的正确路径）。
# 不要写 -lstdc++：交叉 sysroot 里只有 libstdc++.so.6，没有无版本号的 .so 符号链接。
set_target_properties(zimg_harness PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(zimg_harness PRIVATE zimg m)
target_compile_options(zimg_harness PRIVATE ${RVV_FLAGS})

add_test(NAME zimg_harness COMMAND zimg_harness)

add_library(tamias_drawing STATIC
  drawing.cpp
  dxf_reader.cpp
  zip_archive.cpp
  dwf_reader.cpp
)
add_library(tamias::drawing ALIAS tamias_drawing)
target_include_directories(tamias_drawing PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_drawing
  PUBLIC
    tamias::base
    tamias::math
)

# DWF / DWFx 是 ZIP 包，压缩条目要 zlib。桌面构建带上；万一没有（例如某些
# 交叉编译）也照样编得过，只是读不了 deflate 条目会报错。
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  target_link_libraries(tamias_drawing PRIVATE ZLIB::ZLIB)
  target_compile_definitions(tamias_drawing PRIVATE TAMIAS_HAVE_ZLIB=1)
endif()

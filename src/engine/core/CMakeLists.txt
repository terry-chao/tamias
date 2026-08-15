add_library(tamias_core STATIC log.cpp)
add_library(tamias::core ALIAS tamias_core)
target_include_directories(tamias_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

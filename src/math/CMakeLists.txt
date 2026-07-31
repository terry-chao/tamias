add_library(tamias_math INTERFACE)
add_library(tamias::math ALIAS tamias_math)
target_include_directories(tamias_math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

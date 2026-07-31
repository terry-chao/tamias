add_library(tamias_asset INTERFACE)
add_library(tamias::asset ALIAS tamias_asset)
target_include_directories(tamias_asset INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_asset INTERFACE tamias::graphics)

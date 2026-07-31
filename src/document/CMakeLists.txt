add_library(tamias_document INTERFACE)
add_library(tamias::document ALIAS tamias_document)
target_include_directories(tamias_document INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tamias_document INTERFACE tamias::scene tamias::asset)

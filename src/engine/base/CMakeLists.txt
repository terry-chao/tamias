add_library(tamias_base STATIC
  log.cpp
  executable_directory.cpp
  ../profile/profiling.cpp
  ../profile/timing_session.cpp
  ../profile/timing_scope.cpp
  ../profile/timing_xml_writer.cpp
)
add_library(tamias::base ALIAS tamias_base)
target_include_directories(tamias_base PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_base PUBLIC tamias::tracy)

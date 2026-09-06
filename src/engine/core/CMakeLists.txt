add_library(tamias_core STATIC
  log.cpp
  executable_directory.cpp
  ../profile/timing_session.cpp
  ../profile/timing_scope.cpp
  ../profile/timing_xml_writer.cpp
)
add_library(tamias::core ALIAS tamias_core)
target_include_directories(tamias_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

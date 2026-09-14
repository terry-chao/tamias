# Tracy profiler client (https://github.com/wolfpld/tracy).
#
# Development-only instrumentation. The client is linked for Debug/RelWithDebInfo
# only, so Release and the packaged MSI stay profiler-free, and every埋点 macro
# compiles to nothing when the option is off.

option(TAMIAS_ENABLE_TRACY "Link the Tracy profiler client (dev builds only)" OFF)

if(TAMIAS_ENABLE_TRACY AND EMSCRIPTEN)
  # The WASM viewer has no pthread telemetry and Tracy's client is desktop-only here.
  set(TAMIAS_ENABLE_TRACY OFF)
endif()

# Always provide tamias::tracy so call sites never need generator juggling.
add_library(tamias_tracy INTERFACE)
add_library(tamias::tracy ALIAS tamias_tracy)

if(TAMIAS_ENABLE_TRACY)
  find_package(Tracy CONFIG REQUIRED)
  if(NOT TARGET Tracy::TracyClient)
    message(FATAL_ERROR
      "Tracy found but Tracy::TracyClient is missing. Check the vcpkg tracy port.")
  endif()
  target_link_libraries(tamias_tracy INTERFACE
    "$<$<CONFIG:Debug,RelWithDebInfo>:Tracy::TracyClient>")
  target_compile_definitions(tamias_tracy INTERFACE
    "$<$<CONFIG:Debug,RelWithDebInfo>:TAMIAS_ENABLE_TRACY=1>")
  message(STATUS "Tracy profiler client: ON (Debug/RelWithDebInfo)")
else()
  message(STATUS "Tracy profiler client: OFF")
endif()

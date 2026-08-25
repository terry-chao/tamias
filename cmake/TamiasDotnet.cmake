# Locate nethost / hostfxr headers for embedding the .NET runtime, and publish
# Tamias.Host + sample plugins next to tamias.exe.

function(tamias_find_nethost)
  if(TARGET tamias_nethost)
    return()
  endif()
  set(TAMIAS_NETHOST_FOUND FALSE PARENT_SCOPE)
  find_program(TAMIAS_DOTNET NAMES dotnet)
  if(NOT TAMIAS_DOTNET)
    message(STATUS "Tamias C# plugins: dotnet not found")
    return()
  endif()

  if(WIN32)
    set(_rid win-x64)
    set(_pack Microsoft.NETCore.App.Host.win-x64)
    set(_roots
      "$ENV{DOTNET_ROOT}"
      "$ENV{ProgramFiles}/dotnet"
      "C:/Program Files/dotnet"
    )
  else()
    set(_rid linux-x64)
    set(_pack Microsoft.NETCore.App.Host.linux-x64)
    set(_roots
      "$ENV{DOTNET_ROOT}"
      "/usr/share/dotnet"
      "$ENV{HOME}/.dotnet"
    )
  endif()

  set(_native "")
  foreach(_root IN LISTS _roots)
    if(NOT _root)
      continue()
    endif()
    file(GLOB _candidates "${_root}/packs/${_pack}/*/runtimes/${_rid}/native")
    if(_candidates)
      list(SORT _candidates)
      list(GET _candidates -1 _native)
    endif()
    if(_native)
      break()
    endif()
  endforeach()

  if(NOT _native)
    message(STATUS "Tamias C# plugins: nethost pack not found")
    return()
  endif()
  if(NOT EXISTS "${_native}/nethost.h")
    message(STATUS "Tamias C# plugins: nethost.h missing in ${_native}")
    return()
  endif()

  if(WIN32)
    set(_lib "${_native}/nethost.lib")
    set(_dll "${_native}/nethost.dll")
    if(NOT EXISTS "${_lib}")
      message(STATUS "Tamias C# plugins: nethost.lib missing")
      return()
    endif()
  else()
    set(_lib "${_native}/libnethost.so")
    set(_dll "${_lib}")
    if(NOT EXISTS "${_lib}")
      message(STATUS "Tamias C# plugins: libnethost.so missing")
      return()
    endif()
  endif()

  add_library(tamias_nethost SHARED IMPORTED GLOBAL)
  set_target_properties(tamias_nethost PROPERTIES
    IMPORTED_LOCATION "${_dll}"
    INTERFACE_INCLUDE_DIRECTORIES "${_native}"
  )
  if(WIN32)
    set_target_properties(tamias_nethost PROPERTIES IMPORTED_IMPLIB "${_lib}")
  endif()

  set(TAMIAS_NETHOST_FOUND TRUE PARENT_SCOPE)
  set(TAMIAS_DOTNET "${TAMIAS_DOTNET}" PARENT_SCOPE)
  set(TAMIAS_NETHOST_DIR "${_native}" PARENT_SCOPE)
  message(STATUS "Tamias C# plugins: nethost at ${_native}")
endfunction()

function(tamias_copy_nethost target)
  if(NOT WIN32 OR NOT TAMIAS_NETHOST_FOUND)
    return()
  endif()
  if(NOT TARGET ${target})
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TAMIAS_NETHOST_DIR}/nethost.dll"
            $<TARGET_FILE_DIR:${target}>
    COMMENT "Copying nethost.dll next to ${target}"
  )
endfunction()

function(tamias_publish_csharp target)
  if(NOT TAMIAS_DOTNET)
    return()
  endif()
  if(NOT TARGET ${target})
    return()
  endif()

  set(_host_csproj "${CMAKE_SOURCE_DIR}/csharp/Tamias.Host/Tamias.Host.csproj")
  set(_hello_csproj "${CMAKE_SOURCE_DIR}/plugins/csharp/Tamias.Hello/Tamias.Hello.csproj")
  if(NOT EXISTS "${_host_csproj}")
    return()
  endif()

  add_custom_target(tamias_csharp ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/managed
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/plugins
    COMMAND "${TAMIAS_DOTNET}" publish "${_host_csproj}"
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            --nologo
            -o $<TARGET_FILE_DIR:${target}>/managed
    COMMAND "${TAMIAS_DOTNET}" publish "${_hello_csproj}"
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            --nologo
            -o $<TARGET_FILE_DIR:${target}>/plugins
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/csharp"
    COMMENT "Publishing C# plugin host and sample plugins"
    VERBATIM
  )
  add_dependencies(${target} tamias_csharp)
  tamias_copy_nethost(${target})
endfunction()

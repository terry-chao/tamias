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

# 服务端服务壳（server/csharp/Tamias.Server）：独立进程，**反过来** P/Invoke 原生
# tamias_server.dll。所以发布到原生库所在目录，按约定名就能解析到。
# 与 tamias_publish_csharp 的区别：那个是 C++ 宿主往进程里装 .NET，这个是 .NET 宿主
# 往进程里装 C++——方向相反，见 docs/DECISION-SERVER-LANGUAGE.md §2。
function(tamias_publish_server target)
  if(NOT TAMIAS_DOTNET)
    message(STATUS "Tamias server shell: dotnet not found, skipping")
    return()
  endif()
  if(NOT TARGET ${target})
    return()
  endif()

  set(_csproj "${CMAKE_SOURCE_DIR}/server/csharp/Tamias.Server/Tamias.Server.csproj")
  if(NOT EXISTS "${_csproj}")
    return()
  endif()

  # CONFIGURE_DEPENDS：新加的 .cs 也要算依赖，否则改了源码却不 republish
  # （和 tamias_publish_csharp 踩过的是同一个坑）。
  file(GLOB_RECURSE _cs_inputs CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/server/csharp/*.cs"
    "${CMAKE_SOURCE_DIR}/server/csharp/*.csproj"
    "${CMAKE_SOURCE_DIR}/server/csharp/*.json"
  )
  set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/tamias_server_shell-$<CONFIG>.stamp")
  add_custom_command(
    OUTPUT "${_stamp}"
    COMMAND "${TAMIAS_DOTNET}" publish "${_csproj}"
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            --nologo
            -o $<TARGET_FILE_DIR:${target}>
    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
    DEPENDS ${_cs_inputs}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/server/csharp"
    COMMENT "Publishing the .NET server shell next to tamias_server"
    VERBATIM
  )
  add_custom_target(tamias_server_shell DEPENDS "${_stamp}")
  add_dependencies(${target} tamias_server_shell)
endfunction()

function(tamias_publish_csharp target)
  if(NOT TAMIAS_DOTNET)
    return()
  endif()
  if(NOT TARGET ${target})
    return()
  endif()

  set(_host_csproj "${CMAKE_SOURCE_DIR}/plugin-sdk/csharp/Tamias.Host/Tamias.Host.csproj")
  set(_hello_csproj "${CMAKE_SOURCE_DIR}/plugins/csharp/Tamias.Hello/Tamias.Hello.csproj")
  set(_nurbs_csproj "${CMAKE_SOURCE_DIR}/plugins/csharp/Tamias.Nurbs/Tamias.Nurbs.csproj")
  set(_mcp_csproj "${CMAKE_SOURCE_DIR}/plugin-sdk/csharp/Tamias.Mcp/Tamias.Mcp.csproj")
  if(NOT EXISTS "${_host_csproj}")
    return()
  endif()

  # CONFIGURE_DEPENDS：新加的 .cs 文件也要被当成依赖，否则改了源码却不 republish。
  file(GLOB_RECURSE _cs_inputs CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/plugin-sdk/csharp/*.cs"
    "${CMAKE_SOURCE_DIR}/plugin-sdk/csharp/*.csproj"
    "${CMAKE_SOURCE_DIR}/plugins/csharp/*.cs"
    "${CMAKE_SOURCE_DIR}/plugins/csharp/*.csproj"
    "${CMAKE_SOURCE_DIR}/plugins/csharp/*.json"
    "${CMAKE_SOURCE_DIR}/plugins/csharp/*.svg"
  )
  set(_csharp_stamp "${CMAKE_CURRENT_BINARY_DIR}/tamias_csharp-$<CONFIG>.stamp")
  add_custom_command(
    OUTPUT "${_csharp_stamp}"
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
    COMMAND "${TAMIAS_DOTNET}" publish "${_nurbs_csproj}"
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            --nologo
            -o $<TARGET_FILE_DIR:${target}>/plugins
    # MCP stdio 转发器：AI 客户端拉起的独立进程，和 tamias.exe 并排摆。
    COMMAND "${TAMIAS_DOTNET}" publish "${_mcp_csproj}"
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            --nologo
            -o $<TARGET_FILE_DIR:${target}>
    # 目录式扩展示例：源码 + 清单直接摆进约定目录，加载时现编译。
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/plugins/csharp/Tamias.Sample.Tools"
            $<TARGET_FILE_DIR:${target}>/plugins/Tamias.Sample.Tools
    COMMAND ${CMAKE_COMMAND} -E touch "${_csharp_stamp}"
    DEPENDS ${_cs_inputs}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/plugin-sdk/csharp"
    COMMENT "Publishing C# plugin host and sample plugins"
    VERBATIM
  )
  add_custom_target(tamias_csharp DEPENDS "${_csharp_stamp}")
  add_dependencies(${target} tamias_csharp)
  tamias_copy_nethost(${target})
endfunction()

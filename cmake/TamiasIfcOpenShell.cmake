# Fetch IfcOpenShell and build IfcParse only (no IfcGeom / Python / IfcConvert).
# Official cmake/CMakeLists.txt forces /MT on Windows and pulls OCCT; do not add_subdirectory it.
# Geometry (IfcGeom) will later link the same vcpkg OCCT 7.9.3 used by Tamias.
# Boost is FetchContent'd so vcpkg does not recast OCCT.

if(NOT TAMIAS_USE_FETCHCONTENT)
  message(FATAL_ERROR "IfcOpenShell IfcParse requires TAMIAS_USE_FETCHCONTENT=ON")
endif()

set(TAMIAS_IFCOPENSHELL_GIT_TAG "v0.8.0" CACHE STRING "IfcOpenShell git tag or branch")
set(TAMIAS_IFC_SCHEMAS "2x3;4" CACHE STRING "IFC schemas compiled into IfcParse")

include(FetchContent)

# Header-only modules must be listed here, otherwise Boost CMake omits their include dirs.
set(BOOST_INCLUDE_LIBRARIES
  algorithm
  any
  circular_buffer
  date_time
  dynamic_bitset
  filesystem
  foreach
  iostreams
  lexical_cast
  locale
  logic
  math
  multi_index
  optional
  property_tree
  range
  regex
  scope_exit
  thread
  unordered
  uuid
  variant
)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_SKIP_INSTALL_RULES ON)
set(BOOST_LOCALE_ENABLE_ICU OFF)
set(BOOST_IOSTREAMS_ENABLE_ZLIB OFF)
set(BOOST_IOSTREAMS_ENABLE_BZIP2 OFF)
set(BOOST_IOSTREAMS_ENABLE_LZMA OFF)
set(BOOST_IOSTREAMS_ENABLE_ZSTD OFF)
set(_tamias_saved_shared_libs "${BUILD_SHARED_LIBS}")
set(_tamias_saved_c_compiler "${CMAKE_C_COMPILER}")
set(_tamias_saved_cxx_compiler "${CMAKE_CXX_COMPILER}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
  Boost
  URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Boost)
set(BUILD_SHARED_LIBS "${_tamias_saved_shared_libs}")
# Boost's project() rewrites compilers to absolute paths; CMake then recasts
# the cache and vcpkg rebuilds OCCT. Put the preset values back.
set(CMAKE_C_COMPILER "${_tamias_saved_c_compiler}" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_tamias_saved_cxx_compiler}" CACHE FILEPATH "CXX compiler" FORCE)

FetchContent_Declare(
  ifcopenshell
  GIT_REPOSITORY https://github.com/IfcOpenShell/IfcOpenShell.git
  GIT_TAG ${TAMIAS_IFCOPENSHELL_GIT_TAG}
  GIT_SHALLOW TRUE
  GIT_SUBMODULES ""
)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(ifcopenshell)
if(NOT ifcopenshell_POPULATED)
  FetchContent_Populate(ifcopenshell)
endif()

set(_ifc_parse_dir "${ifcopenshell_SOURCE_DIR}/src/ifcparse")
if(NOT EXISTS "${_ifc_parse_dir}/IfcFile.h")
  message(FATAL_ERROR "IfcOpenShell IfcParse headers missing at ${_ifc_parse_dir}")
endif()

file(GLOB _ifc_parse_h_all "${_ifc_parse_dir}/*.h")
file(GLOB _ifc_parse_cpp_all "${_ifc_parse_dir}/*.cpp")

set(_ifc_parse_h "")
set(_ifc_parse_cpp "")
foreach(_file IN LISTS _ifc_parse_h_all)
  get_filename_component(_name "${_file}" NAME)
  if(_name MATCHES "[0-9]|[Xx]ml")
    continue()
  endif()
  list(APPEND _ifc_parse_h "${_file}")
endforeach()
foreach(_file IN LISTS _ifc_parse_cpp_all)
  get_filename_component(_name "${_file}" NAME)
  if(_name MATCHES "[0-9]|[Xx]ml")
    continue()
  endif()
  # Alignment helpers need Boost.Math quadrature; skip for the IfcParse-only tree dump.
  if(_name STREQUAL "IfcAlignmentHelper.cpp")
    continue()
  endif()
  list(APPEND _ifc_parse_cpp "${_file}")
endforeach()

set(_schema_defs "")
set(_schema_seq "")
foreach(_schema IN LISTS TAMIAS_IFC_SCHEMAS)
  foreach(_suffix IN ITEMS ".h" "-definitions.h" ".cpp" "-schema.cpp")
    set(_schema_file "${_ifc_parse_dir}/Ifc${_schema}${_suffix}")
    if(NOT EXISTS "${_schema_file}")
      message(FATAL_ERROR "IfcOpenShell schema file missing: ${_schema_file}")
    endif()
    if(_suffix MATCHES "\\.cpp$")
      list(APPEND _ifc_parse_cpp "${_schema_file}")
    else()
      list(APPEND _ifc_parse_h "${_schema_file}")
    endif()
  endforeach()
  list(APPEND _schema_defs "HAS_SCHEMA_${_schema}")
  string(APPEND _schema_seq "(${_schema})")
endforeach()

add_library(IfcParse STATIC ${_ifc_parse_cpp} ${_ifc_parse_h})
add_library(IfcOpenShell::IfcParse ALIAS IfcParse)
target_compile_definitions(IfcParse
  PRIVATE
    IFC_PARSE_EXPORTS
  PUBLIC
    BOOST_ALL_NO_LIB
    BOOST_OPTIONAL_USE_OLD_DEFINITION_OF_NONE
    ${_schema_defs}
    "SCHEMA_SEQ=${_schema_seq}"
)
target_include_directories(IfcParse
  PUBLIC
    "${ifcopenshell_SOURCE_DIR}/src"
  PRIVATE
    "${_ifc_parse_dir}"
)
target_link_libraries(IfcParse
  PUBLIC
    Boost::algorithm
    Boost::any
    Boost::circular_buffer
    Boost::date_time
    Boost::dynamic_bitset
    Boost::filesystem
    Boost::foreach
    Boost::iostreams
    Boost::lexical_cast
    Boost::locale
    Boost::logic
    Boost::math
    Boost::multi_index
    Boost::optional
    Boost::property_tree
    Boost::range
    Boost::regex
    Boost::scope_exit
    Boost::thread
    Boost::unordered
    Boost::uuid
    Boost::variant
)
if(WIN32)
  target_link_libraries(IfcParse PUBLIC bcrypt)
  # Official IfcOpenShell cmake defines these globally. FileReader.cpp assigns
  # IfcUtil::path::from_utf8() to std::wstring, and IfcUtil.cpp only provides the
  # wide overloads when _UNICODE is set; without it MSVC reports C2440/C2244.
  target_compile_definitions(IfcParse PUBLIC UNICODE _UNICODE)
endif()
if(MSVC)
  target_compile_options(IfcParse PRIVATE /W0 /bigobj /wd4244 /wd4267 /wd4996 /wd4251)
  # IfcOpenShell is not /permissive- clean; keep it compiling next to Tamias.
  target_compile_options(IfcParse PRIVATE /permissive)
else()
  target_compile_options(IfcParse PRIVATE -w)
endif()
set_target_properties(IfcParse PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  FOLDER "3rdparty"
)

message(STATUS "IfcOpenShell IfcParse: ${ifcopenshell_SOURCE_DIR} schemas=${TAMIAS_IFC_SCHEMAS}")

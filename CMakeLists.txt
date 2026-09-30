cmake_minimum_required(VERSION 3.24)
project(FilmGrainOFX VERSION 0.3.0 LANGUAGES CXX)

# Avoid requiring a separately installed MSVC runtime on the editor's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# CMake 3.30+ deprecates the declared-details form of FetchContent_Populate.
# We intentionally use it here because we only need OpenFX headers and do not
# want to configure/build the entire upstream OpenFX project.
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

# OpenFX is a C API. We only need the official headers, pinned to a known release.
FetchContent_Declare(
  openfx
  URL https://github.com/AcademySoftwareFoundation/openfx/archive/refs/tags/OFX_Release_1.5.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(openfx)
if(NOT openfx_POPULATED)
  FetchContent_Populate(openfx)
endif()

add_library(FilmGrainOFX MODULE
  src/FilmGrainOFX.cpp
  src/film_grain_core.cpp
)

target_include_directories(FilmGrainOFX PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${openfx_SOURCE_DIR}/include
)

target_compile_definitions(FilmGrainOFX PRIVATE NOMINMAX)

if(MSVC)
  target_compile_options(FilmGrainOFX PRIVATE /O2 /EHsc /permissive-)
else()
  target_compile_options(FilmGrainOFX PRIVATE -O3)
endif()

set_target_properties(FilmGrainOFX PROPERTIES
  PREFIX ""
  OUTPUT_NAME "FilmGrainOFX"
  SUFFIX ".ofx"
)

if(WIN32)
  set(OFX_ARCH_DIR "Win64")
elseif(APPLE)
  set(OFX_ARCH_DIR "MacOS")
else()
  set(OFX_ARCH_DIR "Linux-x86-64")
endif()

set(OFX_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/dist/FilmGrainOFX.ofx.bundle")
set(OFX_CONTENTS_DIR "${OFX_BUNDLE_ROOT}/Contents")
set(OFX_BINARY_DIR "${OFX_CONTENTS_DIR}/${OFX_ARCH_DIR}")
set(OFX_RESOURCES_DIR "${OFX_CONTENTS_DIR}/Resources")

file(MAKE_DIRECTORY "${OFX_BINARY_DIR}")
file(MAKE_DIRECTORY "${OFX_RESOURCES_DIR}")

# Multi-config generators (Visual Studio) need per-config output paths to avoid /Release nesting.
set_target_properties(FilmGrainOFX PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${OFX_BINARY_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${OFX_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OFX_BINARY_DIR}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OFX_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${OFX_BINARY_DIR}"
  LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${OFX_BINARY_DIR}"
)

if(APPLE)
  configure_file(cmake/Info.plist.in "${OFX_CONTENTS_DIR}/Info.plist" @ONLY)
endif()

configure_file(LICENSE "${OFX_RESOURCES_DIR}/LICENSE.txt" COPYONLY)
configure_file(THIRD_PARTY_NOTICES.md "${OFX_RESOURCES_DIR}/THIRD_PARTY_NOTICES.md" COPYONLY)

include(CTest)
if(BUILD_TESTING)
  add_executable(filmgrain_core_smoke
    tests/core_smoke.cpp
    src/film_grain_core.cpp
  )
  target_include_directories(filmgrain_core_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(MSVC)
    target_compile_options(filmgrain_core_smoke PRIVATE /O2 /EHsc)
  else()
    target_compile_options(filmgrain_core_smoke PRIVATE -O2)
  endif()
  add_test(NAME filmgrain_core_smoke COMMAND filmgrain_core_smoke)
  add_executable(ofx_lifecycle_smoke tests/ofx_lifecycle_smoke.cpp)
  target_include_directories(ofx_lifecycle_smoke PRIVATE ${openfx_SOURCE_DIR}/include)
  target_link_libraries(ofx_lifecycle_smoke PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(ofx_lifecycle_smoke FilmGrainOFX)
  add_test(NAME ofx_lifecycle_smoke
    COMMAND ofx_lifecycle_smoke "$<TARGET_FILE:FilmGrainOFX>")
endif()

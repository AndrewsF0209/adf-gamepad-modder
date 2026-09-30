cmake_minimum_required(VERSION 3.20)

project(adf_gamepad_mapper LANGUAGES CXX C)

# ---- global settings --------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Self-contained binaries: static CRT (games won't ship vcruntime next to them)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ADF_ARCH_NAME x64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(ADF_ARCH_NAME x86)
else()
  message(FATAL_ERROR "Unsupported pointer size")
endif()

# All final artifacts land in <repo>/artifacts/<arch>/
get_filename_component(ADF_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
set(ADF_ARTIFACT_DIR "${ADF_REPO_ROOT}/artifacts/${ADF_ARCH_NAME}" CACHE PATH "Staging dir for built binaries")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ADF_ARTIFACT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ADF_ARTIFACT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---- third party ------------------------------------------------------------
include(FetchContent)

# Shared build is used by the WPF configurator (P/Invoke); static is linked
# into the proxy DLLs / launcher.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)

if(DEFINED ADF_SDL3_SOURCE_DIR)
  add_subdirectory("${ADF_SDL3_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sdl3-build" EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz
  )
  FetchContent_MakeAvailable(SDL3)
endif()

if(TARGET SDL3::SDL3-static)
  set(ADF_SDL3 SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
  set(ADF_SDL3 SDL3::SDL3)
elseif(TARGET SDL3-static)
  set(ADF_SDL3 SDL3-static)
else()
  message(FATAL_ERROR "SDL3 target not found")
endif()

enable_language(ASM_MASM)

add_compile_definitions(
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
  UNICODE
  _UNICODE
)

if(MSVC)
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:/W3>
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    $<$<COMPILE_LANGUAGE:CXX>:/MP>
    $<$<COMPILE_LANGUAGE:C>:/W3>
  )
endif()

# Stage SDL3.dll next to the other runtime artifacts (for the configurator).
set(ADF_SDL3_SHARED "")
foreach(_t SDL3-shared SDL3::SDL3-shared)
  if(TARGET ${_t})
    set(ADF_SDL3_SHARED ${_t})
  endif()
endforeach()
if(ADF_SDL3_SHARED)
  add_custom_target(adf_sdl_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${ADF_SDL3_SHARED}>"
            "${ADF_ARTIFACT_DIR}/SDL3.dll"
    DEPENDS ${ADF_SDL3_SHARED})
endif()

# ---- components -------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/proxy)
add_subdirectory(src/launcher)
add_subdirectory(src/resident)

enable_testing()
add_subdirectory(src/tools/test_curves)
add_subdirectory(src/tools/test_kbm_host)
add_subdirectory(src/tools/input_probe)

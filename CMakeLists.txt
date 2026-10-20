cmake_minimum_required(VERSION 3.25)
project(NetworkSpeedMonitor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

set(NSM_SOURCE_URL "" CACHE STRING "Source repository URL shown in the About box")
# It is pasted into a C string literal and opened with ShellExecute: only an http(s) URL of plain URL characters
# (no quotes, backslashes, spaces or line breaks).
if(NOT NSM_SOURCE_URL STREQUAL "" AND NOT NSM_SOURCE_URL MATCHES "^https?://[A-Za-z0-9][A-Za-z0-9._~:/?#@!$&'()*+,;=%-]*$")
    message(FATAL_ERROR "NSM_SOURCE_URL must be an http:// or https:// URL without quotes, backslashes or spaces: "
                        "'${NSM_SOURCE_URL}'")
endif()

# Version and About link as macros for the code and for the VERSIONINFO resource (generated, in the build tree).
set(NSM_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(NSM_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(NSM_VERSION_PATCH ${PROJECT_VERSION_PATCH})
configure_file(${CMAKE_SOURCE_DIR}/res/nsm_version.h.in ${CMAKE_BINARY_DIR}/generated/nsm_version.h @ONLY)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/storage)
add_subdirectory(src/report)
add_subdirectory(src/ui)
add_subdirectory(tests)

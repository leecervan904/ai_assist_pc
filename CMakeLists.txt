cmake_minimum_required(VERSION 3.28)
project(SonoWorkstation VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SONO_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SONO_WEB_DIR "${SONO_ROOT_DIR}/web")
set(SONO_MIN_QT_VERSION "6.8.2")

list(APPEND CMAKE_MODULE_PATH "${SONO_ROOT_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

option(SONO_BUILD_TESTS "Build SonoWorkstation C++ unit tests" OFF)
option(SONO_LINUX_X11_BYPASS_WM
  "Use X11BypassWindowManagerHint for the CEF overlay on Linux (VM/light WM only)"
  OFF)

if(WIN32)
  include(platform/Windows)
elseif(APPLE)
  include(platform/macOS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(platform/Linux)
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

include(Dependencies)
include(Packaging)
qt_standard_project_setup()

add_subdirectory(src)

if(SONO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

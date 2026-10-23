include_guard(GLOBAL)

set(SONO_QT_COMPONENTS
  Core
  Gui
  Widgets
  Network
  Multimedia
  MultimediaWidgets
)

# Qt is installed and managed by the host. Prefer an explicitly configured
# Qt6_DIR/CMAKE_PREFIX_PATH, otherwise probe platform-standard locations.
if(NOT DEFINED Qt6_DIR)
  set(_sono_qt_candidates)
  foreach(_pattern IN LISTS SONO_QT_HINTS)
    file(GLOB _matches LIST_DIRECTORIES true "${_pattern}")
    list(APPEND _sono_qt_candidates ${_matches})
  endforeach()

  if(_sono_qt_candidates)
    list(REMOVE_DUPLICATES _sono_qt_candidates)
    list(SORT _sono_qt_candidates ORDER DESCENDING)
    list(APPEND CMAKE_PREFIX_PATH ${_sono_qt_candidates})
  endif()
endif()

find_package(Qt6 ${SONO_MIN_QT_VERSION} REQUIRED COMPONENTS
  ${SONO_QT_COMPONENTS}
)
message(STATUS "Qt6 ${Qt6_VERSION}: ${Qt6_DIR}")

set(CEF_ROOT "${SONO_ROOT_DIR}/.deps/cef" CACHE PATH
  "CEF Standard Binary Distribution root")
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(FATAL_ERROR
    "CEF was not found at ${CEF_ROOT}. Run `mise run deps:sync` first.")
endif()

set(USE_SANDBOX OFF CACHE BOOL "CEF sandbox is not used" FORCE)
if(WIN32)
  set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING
    "CEF runtime library flag" FORCE)
endif()
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
enable_language(C)
if(APPLE)
  enable_language(OBJCXX)
endif()
find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
if(TARGET libcef_dll_wrapper AND WIN32)
  set_target_properties(libcef_dll_wrapper PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
  )
endif()

set(ORT_ROOT "${SONO_ROOT_DIR}/.deps/onnxruntime" CACHE PATH
  "ONNX Runtime SDK root")
if(WIN32)
  if(NOT EXISTS "${ORT_ROOT}/lib/onnxruntime.lib")
    message(FATAL_ERROR
      "ONNX Runtime was not found at ${ORT_ROOT}. Run `mise run deps:sync` first.")
  endif()
  add_library(SonoONNXRuntime INTERFACE)
  target_include_directories(SonoONNXRuntime INTERFACE "${ORT_ROOT}/include")
  target_link_libraries(SonoONNXRuntime INTERFACE
    "${ORT_ROOT}/lib/onnxruntime.lib"
  )
  set(SONO_ONNXRUNTIME_TARGET SonoONNXRuntime)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(NOT EXISTS "${ORT_ROOT}/lib/libonnxruntime.so")
    message(FATAL_ERROR
      "ONNX Runtime was not found at ${ORT_ROOT}. Run `mise run deps:sync` first.")
  endif()
  add_library(SonoONNXRuntime SHARED IMPORTED)
  set_target_properties(SonoONNXRuntime PROPERTIES
    IMPORTED_LOCATION "${ORT_ROOT}/lib/libonnxruntime.so"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include"
  )
  get_target_property(SONO_ONNXRUNTIME_LOCATION SonoONNXRuntime LOCATION)
  set(SONO_ONNXRUNTIME_TARGET SonoONNXRuntime)
elseif(APPLE)
  find_package(onnxruntime CONFIG REQUIRED)
  if(TARGET onnxruntime::onnxruntime)
    set(SONO_ONNXRUNTIME_TARGET onnxruntime::onnxruntime)
  elseif(TARGET onnxruntime)
    set(SONO_ONNXRUNTIME_TARGET onnxruntime)
  else()
    message(FATAL_ERROR
      "onnxruntime was found but did not define a supported CMake target")
  endif()
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "ONNX Runtime target: ${SONO_ONNXRUNTIME_TARGET}")

cmake_minimum_required(VERSION 3.24)
project(SonoWorkstation VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

option(SONO_BUILD_TESTS "Build SonoWorkstation C++ unit tests" OFF)
option(SONO_LINUX_X11_BYPASS_WM
  "Use X11BypassWindowManagerHint for the CEF overlay on Linux (VM/light WM only)"
  OFF)

if(NOT DEFINED CEF_ROOT)
  set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/cef")
endif()
set(SONO_USE_CEF OFF)
if(APPLE AND EXISTS "${CEF_ROOT}/Release/Chromium Embedded Framework.framework")
  set(SONO_USE_CEF ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${CEF_ROOT}/Release/libcef.so")
  set(SONO_USE_CEF ON)
elseif(WIN32 AND EXISTS "${CEF_ROOT}/Release/libcef.lib")
  set(SONO_USE_CEF ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DEFINED PROJECT_ARCH)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(PROJECT_ARCH "arm64" CACHE STRING "CEF target architecture")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(PROJECT_ARCH "x86_64" CACHE STRING "CEF target architecture")
  endif()
endif()

# Auto-discover Qt6 from shared user cache (~/.cache/sono_deps/qt6) or standard locations
if(NOT DEFINED Qt6_DIR)
  file(TO_CMAKE_PATH "$ENV{HOME}" _sono_user_home)
  if(NOT _sono_user_home AND WIN32)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _sono_user_home)
  endif()
  file(GLOB _sono_qt_candidates LIST_DIRECTORIES true
    "${_sono_user_home}/.cache/sono_deps/qt6/6.*/*"
    "${_sono_user_home}/Qt/6.*/*"
    "C:/Qt/6.*/msvc2022_64"
    "/opt/homebrew/opt/qt"
  )
  if(_sono_qt_candidates)
    list(SORT _sono_qt_candidates ORDER DESCENDING)
    list(APPEND CMAKE_PREFIX_PATH ${_sono_qt_candidates})
  endif()
endif()

set(SONO_QT_COMPONENTS
  Core Gui Widgets Network Multimedia MultimediaWidgets
)
find_package(Qt6 REQUIRED COMPONENTS
  ${SONO_QT_COMPONENTS}
)

qt_standard_project_setup()

# CEF (Chromium Embedded Framework) for windowless web rendering.
if((WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    AND EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  set(USE_SANDBOX OFF CACHE BOOL "CEF sandbox is not used" FORCE)
  if(WIN32)
    set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "CEF runtime library flag" FORCE)
  endif()
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
  enable_language(C)
  find_package(CEF REQUIRED)
  if(APPLE)
    enable_language(OBJCXX)
  endif()
  add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
  if(TARGET libcef_dll_wrapper AND WIN32)
    set_target_properties(libcef_dll_wrapper PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    )
  endif()
endif()

if(MSVC)
  add_compile_options(/utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

qt_add_executable(SonoWorkstation WIN32
  src/main.cpp
  src/AppMode.h
  src/PreviewSurface.h
  src/CapturePipeline.h
  src/CapturePipeline.cpp
  src/InferenceEngine.h
  src/InferenceEngine.cpp
  src/PreviewWidget.h
  src/PreviewWidget.cpp
  src/Log.h
  src/VideoFrameUtil.h
  src/HostBridge.h
  src/HostBridge.cpp
  src/CefHost.h
  src/CefHost.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/SelfTest.h
  src/SelfTest.cpp
)

if(NOT DEFINED ORT_ROOT)
  set(ORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime")
endif()
target_include_directories(SonoWorkstation PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(WIN32)
  target_include_directories(SonoWorkstation PRIVATE "${ORT_ROOT}/include")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(onnxruntime SHARED IMPORTED)
  set_target_properties(onnxruntime PROPERTIES
          IMPORTED_LOCATION "${ORT_ROOT}/lib/libonnxruntime.so"
          INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include"
  )
else()
  find_package(onnxruntime CONFIG REQUIRED)
endif()

target_link_libraries(SonoWorkstation PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
  Qt6::Multimedia
  Qt6::MultimediaWidgets
)
if(WIN32 AND EXISTS "${CEF_ROOT}/Release/libcef.lib")
  target_include_directories(SonoWorkstation PRIVATE "${CEF_ROOT}")
  target_compile_definitions(SonoWorkstation PRIVATE
    SONO_USE_CEF=1
    ${CEF_COMPILER_DEFINES}
  )
  target_link_libraries(SonoWorkstation PRIVATE
    libcef_dll_wrapper
    "${CEF_ROOT}/Release/libcef.lib"
    ${CEF_STANDARD_LIBS}
  )
elseif(APPLE AND EXISTS "${CEF_ROOT}/Release/Chromium Embedded Framework.framework")
  target_include_directories(SonoWorkstation PRIVATE "${CEF_INCLUDE_PATH}")
  target_compile_definitions(SonoWorkstation PRIVATE
    SONO_USE_CEF=1
    ${CEF_COMPILER_DEFINES}
  )
  target_link_libraries(SonoWorkstation PRIVATE
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    "-framework Metal"
    "-framework QuartzCore"
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${CEF_ROOT}/Release/libcef.so")
  target_include_directories(SonoWorkstation PRIVATE "${CEF_INCLUDE_PATH}")
  target_compile_definitions(SonoWorkstation PRIVATE
    SONO_USE_CEF=1
    ${CEF_COMPILER_DEFINES}
  )
  if(SONO_LINUX_X11_BYPASS_WM)
    target_compile_definitions(SonoWorkstation PRIVATE SONO_X11_BYPASS_WM=1)
  endif()
  target_link_libraries(SonoWorkstation PRIVATE
    libcef_dll_wrapper
    "${CEF_ROOT}/$<CONFIG>/libcef.so"
    ${CEF_STANDARD_LIBS}
  )
  set_target_properties(SonoWorkstation PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE
  )
endif()
if(WIN32)
  target_link_libraries(SonoWorkstation PRIVATE "${ORT_ROOT}/lib/onnxruntime.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(SonoWorkstation PRIVATE onnxruntime)
else()
  target_link_libraries(SonoWorkstation PRIVATE onnxruntime::onnxruntime)
endif()

set_target_properties(SonoWorkstation PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
  OUTPUT_NAME SonoWorkstation
)

if(NOT DEFINED WORKSTATION_HTML)
  set(WORKSTATION_HTML "${CMAKE_CURRENT_SOURCE_DIR}/workstation.html")
endif()
if(NOT EXISTS "${WORKSTATION_HTML}")
  message(FATAL_ERROR "找不到工作站页面: ${WORKSTATION_HTML}")
endif()

set(SONO_MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/models")
set(SONO_MEDIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/media")

add_custom_command(TARGET SonoWorkstation POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SonoWorkstation>/web"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WORKSTATION_HTML}"
    "$<TARGET_FILE_DIR:SonoWorkstation>/web/workstation.html"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SONO_MODELS_DIR}/XT-7-640.onnx"
    "$<TARGET_FILE_DIR:SonoWorkstation>/XT-7-640.onnx"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SONO_MODELS_DIR}/XT-7-640.ini"
    "$<TARGET_FILE_DIR:SonoWorkstation>/XT-7-640.ini"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SONO_MODELS_DIR}/thyroid.onnx"
    "$<TARGET_FILE_DIR:SonoWorkstation>/thyroid.onnx"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SONO_MODELS_DIR}/thyroid.ini"
    "$<TARGET_FILE_DIR:SonoWorkstation>/thyroid.ini"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SONO_MEDIA_DIR}/XT.mp4"
    "$<TARGET_FILE_DIR:SonoWorkstation>/XT.mp4"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${SONO_MEDIA_DIR}/test-1080p120.mp4"
    "$<TARGET_FILE_DIR:SonoWorkstation>/test-1080p120.mp4"
  COMMENT "Copy workstation.html, ONNX models (.onnx/.ini) and test media (.mp4)"
)

if(WIN32)
  add_custom_command(TARGET SonoWorkstation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll"
      "$<TARGET_FILE_DIR:SonoWorkstation>/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime_providers_shared.dll"
      "$<TARGET_FILE_DIR:SonoWorkstation>/onnxruntime_providers_shared.dll"
  )
  if(EXISTS "${CEF_ROOT}/Release/libcef.dll")
    add_custom_command(TARGET SonoWorkstation POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CEF_ROOT}/Release"
        "$<TARGET_FILE_DIR:SonoWorkstation>"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CEF_ROOT}/Resources"
        "$<TARGET_FILE_DIR:SonoWorkstation>"
      COMMENT "Copy CEF runtime and resources"
    )
  endif()
endif()

if(WIN32)
  find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS "${Qt6_DIR}/../../../tools/Qt6/bin" "${Qt6_DIR}/../../../bin"
  )
  if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET SonoWorkstation POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        "$<IF:$<CONFIG:Debug>,--debug,--release>"
        --no-translations
        "$<TARGET_FILE:SonoWorkstation>"
      COMMENT "Deploy Qt runtime beside the executable"
    )
  endif()
endif()

if(APPLE AND EXISTS "${CEF_ROOT}/Release/Chromium Embedded Framework.framework")
  set(_cef_app_bundle "$<TARGET_BUNDLE_DIR:SonoWorkstation>")
  set(_cef_config_dir "${CEF_ROOT}/$<CONFIG>")

  set(_cef_framework_dir
      "${_cef_app_bundle}/Contents/Frameworks/Chromium Embedded Framework.framework")
  add_custom_command(TARGET SonoWorkstation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_cef_framework_dir}"
    VERBATIM
  )
  COPY_MAC_FRAMEWORK(SonoWorkstation "${_cef_config_dir}" "${_cef_app_bundle}")

  set(CEF_HELPER_TARGET "SonoWorkstation_Helper")
  set(CEF_HELPER_OUTPUT_NAME "SonoWorkstation Helper")
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
    set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")
    set(_helper_info_plist
        "${CMAKE_CURRENT_BINARY_DIR}/SonoWorkstation-helper-Info${_target_suffix}.plist")
    file(READ
      "${CMAKE_CURRENT_SOURCE_DIR}/src/SonoWorkstation-helper-Info.plist.in"
      _plist_contents)
    string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_output_name}"
      _plist_contents ${_plist_contents})
    string(REPLACE "\${PRODUCT_NAME}" "${_helper_output_name}"
      _plist_contents ${_plist_contents})
    string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_plist_suffix}"
      _plist_contents ${_plist_contents})
    string(REPLACE "\${VERSION_SHORT}" "${PROJECT_VERSION}"
      _plist_contents ${_plist_contents})
    file(WRITE ${_helper_info_plist} "${_plist_contents}")

    add_executable(${_helper_target} MACOSX_BUNDLE
      src/CefHelperMac.cpp
    )
    target_include_directories(${_helper_target} PRIVATE
      "${CEF_INCLUDE_PATH}"
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(${_helper_target} PRIVATE ${CEF_COMPILER_DEFINES})
    target_link_libraries(${_helper_target} PRIVATE
      libcef_dll_wrapper
      ${CEF_STANDARD_LIBS}
    )
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
      OUTPUT_NAME "${_helper_output_name}"
    )
    add_dependencies(SonoWorkstation "${_helper_target}")
    add_custom_command(TARGET SonoWorkstation POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:${_helper_target}>"
        "${_cef_app_bundle}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM
    )
  endforeach()

  add_custom_command(TARGET SonoWorkstation POST_BUILD
    COMMAND /usr/bin/codesign --force --deep --sign - "${_cef_app_bundle}"
    COMMENT "Ad-hoc codesign CEF app bundle"
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${CEF_ROOT}/Release/libcef.so")
  add_custom_command(TARGET SonoWorkstation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CEF_ROOT}/$<CONFIG>"
      "$<TARGET_FILE_DIR:SonoWorkstation>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CEF_ROOT}/Resources"
      "$<TARGET_FILE_DIR:SonoWorkstation>"
    COMMENT "Copy CEF Linux runtime and resources"
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  get_target_property(_onnxrt_location onnxruntime LOCATION)
  if(_onnxrt_location AND EXISTS "${_onnxrt_location}")
    get_filename_component(_onnxrt_real "${_onnxrt_location}" REALPATH)
    get_filename_component(_onnxrt_real_name "${_onnxrt_real}" NAME)
    add_custom_command(TARGET SonoWorkstation POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_onnxrt_real}"
        "$<TARGET_FILE_DIR:SonoWorkstation>/"
      COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:SonoWorkstation>/libonnxruntime.so.1"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${_onnxrt_real_name}"
        "$<TARGET_FILE_DIR:SonoWorkstation>/libonnxruntime.so.1"
      COMMENT "Copy ONNX Runtime shared library and symlink"
    )
  endif()
endif()

if(SONO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()


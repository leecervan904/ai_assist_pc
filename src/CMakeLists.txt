qt_add_executable(SonoWorkstation WIN32
  main.cpp
  AppMode.h
  AssetPaths.h
  PreviewSurface.h
  CapturePipeline.h
  CapturePipeline.cpp
  InferenceEngine.h
  InferenceEngine.cpp
  PreviewWidget.h
  PreviewWidget.cpp
  Log.h
  VideoFrameUtil.h
  HostBridge.h
  HostBridge.cpp
  CefHost.h
  CefHost.cpp
  SonoCefApp.h
  MainWindow.h
  MainWindow.cpp
  SelfTest.h
  SelfTest.cpp
)

target_include_directories(SonoWorkstation PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(SonoWorkstation PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  ${SONO_ONNXRUNTIME_TARGET}
)

sono_set_project_warnings(SonoWorkstation)
sono_enable_sanitizers(SonoWorkstation)
sono_platform_configure_target(SonoWorkstation)

if(COMMAND sono_platform_create_helpers)
  sono_platform_create_helpers(SonoWorkstation)
endif()

sono_stage_runtime_assets(SonoWorkstation)
sono_platform_deploy_runtime(SonoWorkstation)

set_target_properties(SonoWorkstation PROPERTIES
  OUTPUT_NAME SonoWorkstation
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

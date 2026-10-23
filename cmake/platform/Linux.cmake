include_guard(GLOBAL)

set(SONO_QT_HINTS
  "$ENV{HOME}/Qt/6.*/gcc_64"
  "/opt/Qt/6.*/gcc_64"
)

function(sono_platform_configure_target target_name)
  target_include_directories(${target_name} PRIVATE "${CEF_INCLUDE_PATH}")
  target_compile_definitions(${target_name} PRIVATE
    SONO_USE_CEF=1
    ${CEF_COMPILER_DEFINES}
  )
  if(SONO_LINUX_X11_BYPASS_WM)
    target_compile_definitions(${target_name} PRIVATE SONO_X11_BYPASS_WM=1)
  endif()
  target_link_libraries(${target_name} PRIVATE
    libcef_dll_wrapper
    "${CEF_ROOT}/$<CONFIG>/libcef.so"
    ${CEF_STANDARD_LIBS}
  )
  set_target_properties(${target_name} PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE
  )
endfunction()

function(sono_platform_deploy_runtime target_name)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CEF_ROOT}/$<CONFIG>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CEF_ROOT}/Resources"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Deploy CEF runtime and resources"
  )

  if(SONO_ONNXRUNTIME_LOCATION AND EXISTS "${SONO_ONNXRUNTIME_LOCATION}")
    get_filename_component(_onnxrt_real "${SONO_ONNXRUNTIME_LOCATION}" REALPATH)
    get_filename_component(_onnxrt_real_name "${_onnxrt_real}" NAME)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_onnxrt_real}"
        "$<TARGET_FILE_DIR:${target_name}>/"
      COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:${target_name}>/libonnxruntime.so.1"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${_onnxrt_real_name}"
        "$<TARGET_FILE_DIR:${target_name}>/libonnxruntime.so.1"
      COMMENT "Deploy ONNX Runtime shared library and symlink"
    )
  endif()
endfunction()

include_guard(GLOBAL)

set(SONO_QT_HINTS
  "$ENV{USERPROFILE}/Qt/6.*/msvc2022_64"
  "C:/Qt/6.*/msvc2022_64"
)

function(sono_platform_configure_target target_name)
  target_include_directories(${target_name} PRIVATE "${CEF_ROOT}")
  target_compile_definitions(${target_name} PRIVATE
    SONO_USE_CEF=1
    ${CEF_COMPILER_DEFINES}
  )
  target_link_libraries(${target_name} PRIVATE
    libcef_dll_wrapper
    "${CEF_ROOT}/Release/libcef.lib"
    ${CEF_STANDARD_LIBS}
  )
  set_target_properties(${target_name} PROPERTIES
    WIN32_EXECUTABLE TRUE
  )
endfunction()

function(sono_platform_deploy_runtime target_name)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll"
      "$<TARGET_FILE_DIR:${target_name}>/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime_providers_shared.dll"
      "$<TARGET_FILE_DIR:${target_name}>/onnxruntime_providers_shared.dll"
    COMMENT "Deploy ONNX Runtime DLLs"
  )

  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CEF_ROOT}/Release"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CEF_ROOT}/Resources"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Deploy CEF runtime and resources"
  )

  find_program(SONO_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt
    HINTS "${Qt6_DIR}/../../../tools/Qt6/bin" "${Qt6_DIR}/../../../bin"
  )
  if(SONO_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND "${SONO_WINDEPLOYQT_EXECUTABLE}"
        "$<IF:$<CONFIG:Debug>,--debug,--release>"
        --no-translations
        "$<TARGET_FILE:${target_name}>"
      COMMENT "Deploy Qt runtime beside the executable"
    )
  else()
    message(WARNING
      "windeployqt was not found; the Windows build may not run outside an IDE")
  endif()
endfunction()

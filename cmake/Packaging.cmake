include_guard(GLOBAL)

set(SONO_RUNTIME_MODEL_FILES
  XT-7-640.onnx
  XT-7-640.ini
  thyroid.onnx
  thyroid.ini
)
set(SONO_RUNTIME_MEDIA_FILES
  XT.mp4
  test-1080p120.mp4
)

function(sono_stage_runtime_assets target_name)
  if(NOT EXISTS "${SONO_WEB_DIR}/workstation.html")
    message(FATAL_ERROR "工作站在线页面缺失: ${SONO_WEB_DIR}/workstation.html")
  endif()

  foreach(_model IN LISTS SONO_RUNTIME_MODEL_FILES)
    if(NOT EXISTS "${SONO_ROOT_DIR}/assets/models/${_model}")
      message(FATAL_ERROR
        "运行时模型文件缺失: assets/models/${_model}. 运行 `mise run deps:sync`.")
    endif()
  endforeach()
  foreach(_media IN LISTS SONO_RUNTIME_MEDIA_FILES)
    if(NOT EXISTS "${SONO_ROOT_DIR}/assets/media/${_media}")
      message(FATAL_ERROR
        "运行时测试视频缺失: assets/media/${_media}. 运行 `mise run deps:lfs`.")
    endif()
  endforeach()

  set(_staging_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:${target_name}>/web"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${SONO_WEB_DIR}"
      "$<TARGET_FILE_DIR:${target_name}>/web"
  )
  foreach(_model IN LISTS SONO_RUNTIME_MODEL_FILES)
    list(APPEND _staging_commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SONO_ROOT_DIR}/assets/models/${_model}"
        "$<TARGET_FILE_DIR:${target_name}>/${_model}"
    )
  endforeach()
  foreach(_media IN LISTS SONO_RUNTIME_MEDIA_FILES)
    list(APPEND _staging_commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SONO_ROOT_DIR}/assets/media/${_media}"
        "$<TARGET_FILE_DIR:${target_name}>/${_media}"
    )
  endforeach()

  add_custom_command(TARGET ${target_name} POST_BUILD
    ${_staging_commands}
    COMMENT "Stage web UI, ONNX models and test media"
    VERBATIM
  )
endfunction()

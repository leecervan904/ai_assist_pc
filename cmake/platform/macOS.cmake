include_guard(GLOBAL)

set(SONO_QT_HINTS
  "$ENV{HOME}/Qt/6.*/*"
  "/opt/homebrew/opt/qt"
  "/usr/local/opt/qt"
)

function(sono_platform_configure_target target_name)
  target_include_directories(${target_name} PRIVATE "${CEF_INCLUDE_PATH}")
  target_compile_definitions(${target_name} PRIVATE
    SONO_USE_CEF=1
    ${CEF_COMPILER_DEFINES}
  )
  target_link_libraries(${target_name} PRIVATE
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    "-framework Metal"
    "-framework QuartzCore"
  )
  set_target_properties(${target_name} PROPERTIES
    MACOSX_BUNDLE TRUE
  )
endfunction()

function(sono_platform_create_helpers target_name)
  set(_cef_app_bundle "$<TARGET_BUNDLE_DIR:${target_name}>")
  set(CEF_HELPER_TARGET "${target_name}_Helper")
  set(CEF_HELPER_OUTPUT_NAME "${target_name} Helper")

  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
    set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")
    set(_helper_info_plist
      "${CMAKE_BINARY_DIR}/SonoWorkstation-helper-Info${_target_suffix}.plist")
    file(READ
      "${SONO_ROOT_DIR}/cmake/packaging/macOS/Helper-Info.plist.in"
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
      "${SONO_ROOT_DIR}/src/CefHelperMac.cpp"
    )
    target_include_directories(${_helper_target} PRIVATE
      "${CEF_INCLUDE_PATH}"
      "${SONO_ROOT_DIR}/src"
    )
    target_compile_definitions(${_helper_target} PRIVATE
      ${CEF_COMPILER_DEFINES}
    )
    target_link_libraries(${_helper_target} PRIVATE
      libcef_dll_wrapper
      ${CEF_STANDARD_LIBS}
    )
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
      OUTPUT_NAME "${_helper_output_name}"
    )
    add_dependencies(${target_name} ${_helper_target})
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_BUNDLE_DIR:${_helper_target}>"
        "${_cef_app_bundle}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM
    )
  endforeach()
endfunction()

function(sono_platform_deploy_runtime target_name)
  set(_cef_app_bundle "$<TARGET_BUNDLE_DIR:${target_name}>")
  set(_cef_config_dir "${CEF_ROOT}/$<CONFIG>")
  set(_cef_framework_dir
    "${_cef_app_bundle}/Contents/Frameworks/Chromium Embedded Framework.framework")

  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${_cef_framework_dir}"
    VERBATIM
  )
  COPY_MAC_FRAMEWORK(${target_name} "${_cef_config_dir}" "${_cef_app_bundle}")

  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND /usr/bin/codesign --force --deep --sign - "${_cef_app_bundle}"
    COMMENT "Ad-hoc codesign CEF app bundle"
  )
endfunction()

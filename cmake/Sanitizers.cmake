# ============================================================================
# Sanitizers.cmake - Optional ASan / UBSan Support for Debug & CI Builds
# ============================================================================

function(sono_enable_sanitizers target_name)
  option(SONO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
  option(SONO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    set(SANITIZERS "")
    if(SONO_ENABLE_ASAN)
      list(APPEND SANITIZERS "address")
    endif()
    if(SONO_ENABLE_UBSAN)
      list(APPEND SANITIZERS "undefined")
    endif()

    list(JOIN SANITIZERS "," LIST_OF_SANITIZERS)
    if(LIST_OF_SANITIZERS)
      target_compile_options(${target_name} PRIVATE -fsanitize=${LIST_OF_SANITIZERS} -fno-omit-frame-pointer)
      target_link_options(${target_name} PRIVATE -fsanitize=${LIST_OF_SANITIZERS})
    endif()
  elseif(MSVC AND SONO_ENABLE_ASAN)
    target_compile_options(${target_name} PRIVATE /fsanitize=address)
  endif()
endfunction()

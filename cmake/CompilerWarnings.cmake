# ============================================================================
# CompilerWarnings.cmake - Standardized Cross-Platform Compiler Warnings
# ============================================================================

function(sono_set_project_warnings target_name)
  option(SONO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

  set(MSVC_WARNINGS
    /W4
    /w14242
    /w14254
    /w14263
    /w14265
    /w14287
    /we4289
    /w14296
    /w14311
    /w14545
    /w14546
    /w14547
    /w14549
    /w14555
    /w14640
    /w14826
    /w14905
    /w14906
    /w14928
  )

  set(CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
  )

  set(GCC_WARNINGS
    ${CLANG_WARNINGS}
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
  )

  if(SONO_WARNINGS_AS_ERRORS)
    list(APPEND MSVC_WARNINGS /WX)
    list(APPEND CLANG_WARNINGS -Werror)
    list(APPEND GCC_WARNINGS -Werror)
  endif()

  if(MSVC)
    set(PROJECT_WARNINGS ${MSVC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    set(PROJECT_WARNINGS ${CLANG_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_WARNINGS ${GCC_WARNINGS})
  endif()

  target_compile_options(${target_name} PRIVATE ${PROJECT_WARNINGS})
endfunction()

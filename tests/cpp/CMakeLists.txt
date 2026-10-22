find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(sono_smoke_test
  test_smoke.cpp
)

target_include_directories(sono_smoke_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(sono_smoke_test PRIVATE
  Qt6::Core
  Qt6::Test
)

add_test(NAME SonoSmokeTest COMMAND sono_smoke_test)

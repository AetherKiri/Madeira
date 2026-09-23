cmake_minimum_required(VERSION 3.20)

project(MadeiraSE
  VERSION 0.1.0
  LANGUAGES C
)

find_package(Threads REQUIRED)

option(MADEIRA_SE_BUILD_TESTS "Build Madeira-SE unit tests" ON)
option(MADEIRA_SE_BUILD_TOOLS "Build Madeira-SE command-line tools" ON)
option(MADEIRA_SE_BUILD_SHARED_RUNTIME
  "Build the standalone Wine-loadable runtime library" ON)

set(MADEIRA_SE_CORE_SOURCES
  src/madeira_se.c
  src/madeira_se_cpu.c
  src/madeira_se_qemu.c
  src/madeira_se_wine.c
  src/madeira_se_wine_cpu.c
  src/madeira_se_wine_cpu_host.c
  src/madeira_se_wine_runtime.c
)

add_library(madeira_se_core STATIC ${MADEIRA_SE_CORE_SOURCES})

target_compile_features(madeira_se_core PUBLIC c_std_11)
target_include_directories(madeira_se_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(madeira_se_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(madeira_se_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
  )
endif()

set_target_properties(madeira_se_core PROPERTIES
  OUTPUT_NAME madeira_se_core
  POSITION_INDEPENDENT_CODE ON
)

if(MADEIRA_SE_BUILD_SHARED_RUNTIME)
  add_library(madeira_se_runtime SHARED ${MADEIRA_SE_CORE_SOURCES})
  target_compile_features(madeira_se_runtime PUBLIC c_std_11)
  target_include_directories(madeira_se_runtime
    PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  target_link_libraries(madeira_se_runtime
    PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(madeira_se_runtime PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
    )
  endif()
  set_target_properties(madeira_se_runtime PROPERTIES
    OUTPUT_NAME madeira_se_runtime
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
  )
endif()

if(MADEIRA_SE_BUILD_TOOLS)
  add_executable(madeira-se-probe
    tools/madeira_se_probe.c
  )
  target_link_libraries(madeira-se-probe PRIVATE madeira_se_core)
  target_compile_features(madeira-se-probe PRIVATE c_std_11)

  add_executable(madeira-se-qemu-backend-probe
    tools/qemu_backend_probe.c
  )
  target_link_libraries(madeira-se-qemu-backend-probe PRIVATE madeira_se_core)
  target_compile_features(madeira-se-qemu-backend-probe PRIVATE c_std_11)

  add_executable(madeira-se-run
    tools/madeira_se_run.c
  )
  target_link_libraries(madeira-se-run PRIVATE madeira_se_core)
  target_compile_features(madeira-se-run PRIVATE c_std_11)

  if(MADEIRA_SE_BUILD_SHARED_RUNTIME)
    add_executable(madeira-se-qemu-runtime-probe
      tools/qemu_runtime_probe.c
    )
    target_link_libraries(madeira-se-qemu-runtime-probe
      PRIVATE madeira_se_runtime)
    target_compile_features(madeira-se-qemu-runtime-probe PRIVATE c_std_11)
  endif()
endif()

if(MADEIRA_SE_BUILD_TESTS)
  enable_testing()

  if(MADEIRA_SE_BUILD_TOOLS)
    add_test(NAME madeira-se-run-help COMMAND madeira-se-run --help)
    set_tests_properties(madeira-se-run-help PROPERTIES
      PASS_REGULAR_EXPRESSION "d3d9-backend")
    add_test(NAME madeira-se-run-invalid-d3d9-backend
      COMMAND madeira-se-run --d3d9-backend invalid)
    set_tests_properties(madeira-se-run-invalid-d3d9-backend PROPERTIES
      WILL_FAIL TRUE)
  endif()

  add_executable(madeira-se-core-tests
    tests/test_madeira_se.c
  )
  target_link_libraries(madeira-se-core-tests PRIVATE madeira_se_core)
  target_compile_features(madeira-se-core-tests PRIVATE c_std_11)

  add_executable(madeira-se-cpu-tests
    tests/test_cpu.c
  )
  target_link_libraries(madeira-se-cpu-tests PRIVATE madeira_se_core)
  target_compile_features(madeira-se-cpu-tests PRIVATE c_std_11)

  add_executable(madeira-se-wine-layout-tests
    tests/test_wine_layout.c
  )
  target_link_libraries(madeira-se-wine-layout-tests PRIVATE madeira_se_core)
  target_compile_features(madeira-se-wine-layout-tests PRIVATE c_std_11)

  add_executable(madeira-se-wine-cpu-tests
    tests/test_wine_cpu.c
  )
  target_link_libraries(madeira-se-wine-cpu-tests PRIVATE madeira_se_core)
  target_compile_features(madeira-se-wine-cpu-tests PRIVATE c_std_11)

  add_executable(madeira-se-wine-cpu-host-tests
    tests/test_wine_cpu_host.c
  )
  target_link_libraries(madeira-se-wine-cpu-host-tests PRIVATE madeira_se_core)
  target_compile_features(madeira-se-wine-cpu-host-tests PRIVATE c_std_11)

  add_executable(madeira-se-wow64-address-tests
    tests/test_wow64_address.c
  )
  target_include_directories(madeira-se-wow64-address-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../wine/include"
  )
  target_compile_definitions(madeira-se-wow64-address-tests PRIVATE
    MADEIRA_SE_DARWIN_ARM64=1
  )
  target_compile_features(madeira-se-wow64-address-tests PRIVATE c_std_11)

  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(test_target
        madeira-se-core-tests
        madeira-se-cpu-tests
        madeira-se-wine-layout-tests
        madeira-se-wine-cpu-tests
        madeira-se-wine-cpu-host-tests
        madeira-se-wow64-address-tests)
      target_compile_options(${test_target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
      )
    endforeach()
  endif()

  add_test(NAME madeira-se-core COMMAND madeira-se-core-tests)
  add_test(NAME madeira-se-cpu COMMAND madeira-se-cpu-tests)
  add_test(NAME madeira-se-wine-layout COMMAND madeira-se-wine-layout-tests)
  add_test(NAME madeira-se-wine-cpu COMMAND madeira-se-wine-cpu-tests)
  add_test(NAME madeira-se-wine-cpu-host COMMAND madeira-se-wine-cpu-host-tests)
  add_test(NAME madeira-se-wow64-address COMMAND madeira-se-wow64-address-tests)
endif()

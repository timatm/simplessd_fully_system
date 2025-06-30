# tests/CMakeLists.txt
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
# add_compile_options(-g -O0 -fsanitize=address)
enable_testing()
include(GoogleTest)

# 抓取 tests/*.cc
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

foreach(src ${TEST_SRC})
    # test_lbnPool.cc -> test_lbnPool
    get_filename_component(test_name ${src} NAME_WE)

    add_executable(${test_name} ${src})
    target_link_libraries(${test_name}
        PRIVATE gtest gtest_main mapping_lib pthread)

    # 每支檔案算一群，前綴就是檔名.
    gtest_discover_tests(
        ${test_name}
        TEST_PREFIX ${test_name}.
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endforeach()

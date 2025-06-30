cmake_minimum_required(VERSION 3.14)
project(mapping_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 指定 include 目錄
include_directories(${PROJECT_SOURCE_DIR}/include)

# 加入 src 子目錄
add_subdirectory(src)

# 加入 tests 子目錄
enable_testing()
add_subdirectory(tests)

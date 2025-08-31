cmake_minimum_required(VERSION 3.10)
project(mydb_static LANGUAGES C CXX)

# 1) 不要全静态（删掉 -static）
# set(CMAKE_EXE_LINKER_FLAGS "-static")
# set(BUILD_SHARED_LIBS OFF)

# 2) 语言标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall)

# 3) 检测 aligned_alloc
include(CheckSymbolExists)
check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
if(NOT HAVE_ALIGNED_ALLOC)
  add_definitions(-DNO_ALIGNED_ALLOC)
endif()

# 4) include 路径
include_directories(
  ${CMAKE_SOURCE_DIR}/../firmware
  ${CMAKE_SOURCE_DIR}/../include
)

# 5) 静态子库（保留 .a 没问题）
file(GLOB NVME_SRC src/db_nvme/nvme_test.cc)
add_library(mydb_nvme STATIC ${NVME_SRC})



# 或者針對目標（更精準）



target_include_directories(mydb_nvme PRIVATE
  ${CMAKE_SOURCE_DIR}/src/db_nvme
  ${CMAKE_SOURCE_DIR}/src/db_nvme/lib
  ${CMAKE_SOURCE_DIR}/src/db_nvme/libnvme
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/db_engine
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(mydb_nvme PRIVATE
  ${CMAKE_SOURCE_DIR}/src/db_nvme/lib/libnvme.a
  pthread
)

# 6) 可执行文件
file(GLOB ENGINE_SRC src/db_engine/*.cc)
add_executable(mydb_engine ${ENGINE_SRC})

target_include_directories(mydb_engine PRIVATE
  ${CMAKE_SOURCE_DIR}/src/db_nvme
  ${CMAKE_SOURCE_DIR}/src/db_nvme/lib
  ${CMAKE_SOURCE_DIR}/src/db_nvme/libnvme
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/db_engine
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# 7) firmware 子模块
add_subdirectory(${CMAKE_SOURCE_DIR}/../firmware firmware_build)

target_link_libraries(mydb_engine PRIVATE mydb_nvme pthread firmware_lib)

# 8) 仅对可执行开启 ASan（关键！）
target_compile_options(mydb_engine PRIVATE
  -g -O1 -fno-omit-frame-pointer
  -fsanitize=address,undefined
)
target_link_options(mydb_engine PRIVATE
  -fsanitize=address,undefined
)

target_compile_options(mydb_engine PRIVATE -fsanitize=address -fno-omit-frame-pointer)
target_link_options(mydb_engine    PRIVATE -fsanitize=address)
message(STATUS "✅ ASan build (glibc, dynamic) enabled")

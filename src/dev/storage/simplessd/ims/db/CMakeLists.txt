cmake_minimum_required(VERSION 3.5)
project(mydb_static LANGUAGES C CXX)

# ────── 1. 強制使用 musl 工具鏈進行完全靜態連結 ──────
set(CMAKE_C_COMPILER /home/timatm/x86_64-linux-musl-cross/bin/x86_64-linux-musl-gcc)
set(CMAKE_CXX_COMPILER /home/timatm/x86_64-linux-musl-cross/bin/x86_64-linux-musl-g++)

set(CMAKE_EXE_LINKER_FLAGS "-static")
set(BUILD_SHARED_LIBS OFF)

# ────── 2. 語言標準設定 ──────
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# add_compile_options(-Wall -Wextra -Wpedantic -static)
add_compile_options(-Wall -Wpedantic -static)
# ────── 3. 偵測 aligned_alloc ──────
include(CheckSymbolExists)
check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
if(NOT HAVE_ALIGNED_ALLOC)
    add_definitions(-DNO_ALIGNED_ALLOC)
endif()

# ────── 4. 靜態子庫 mydb_nvme ──────
file(GLOB NVME_SRC
     src/db_nvme/*.c
     src/db_nvme/*.cc)

add_library(mydb_nvme STATIC ${NVME_SRC})

target_include_directories(mydb_nvme PRIVATE
    ${CMAKE_SOURCE_DIR}/src/db_nvme
    ${CMAKE_SOURCE_DIR}/src/db_nvme/lib
    ${CMAKE_SOURCE_DIR}/src/db_nvme/libnvme
    ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(mydb_nvme PRIVATE
    ${CMAKE_SOURCE_DIR}/src/db_nvme/lib/libnvme.a
    pthread)

# ────── 5. 執行檔 mydb_engine ──────
file(GLOB ENGINE_SRC
     src/db_engine/*.cc)

add_executable(mydb_engine ${ENGINE_SRC})

target_include_directories(mydb_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/db_nvme
    ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(mydb_engine PRIVATE mydb_nvme pthread)

# ────── 6. 完成提示 ──────
message(STATUS "✅ 完全靜態 (musl) 編譯模式啟用")

cmake_minimum_required(VERSION 3.10)
project(mydb_static LANGUAGES C CXX)

# ────── 1. 靜態編譯設置 ──────
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(BUILD_SHARED_LIBS OFF)

# ────── 2. 語言標準設定 ──────
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall)

# ────── 3. 偵測 aligned_alloc ──────
include(CheckSymbolExists)
check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
if(NOT HAVE_ALIGNED_ALLOC)
    add_definitions(-DNO_ALIGNED_ALLOC)
endif()

# ────── 4. 包含路徑 ──────
include_directories(
    ${CMAKE_SOURCE_DIR}/../firmware
    ${CMAKE_SOURCE_DIR}/../include
)

# ────── 5. 靜態子庫 mydb_nvme ──────
file(GLOB NVME_SRC
     src/db_nvme/nvme_test.cc)

add_library(mydb_nvme STATIC ${NVME_SRC})

target_include_directories(mydb_nvme PRIVATE
    ${CMAKE_SOURCE_DIR}/src/db_nvme
    ${CMAKE_SOURCE_DIR}/src/db_nvme/lib
    ${CMAKE_SOURCE_DIR}/src/db_nvme/libnvme
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/db_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(mydb_nvme PRIVATE
    ${CMAKE_SOURCE_DIR}/src/db_nvme/lib/libnvme.a
    pthread)

# ────── 6. 執行檔 mydb_engine ──────
file(GLOB ENGINE_SRC
     src/db_engine/*.cc)

add_executable(mydb_engine ${ENGINE_SRC})

target_include_directories(mydb_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/db_nvme
    ${CMAKE_SOURCE_DIR}/src/db_nvme/lib
    ${CMAKE_SOURCE_DIR}/src/db_nvme/libnvme
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/db_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# 加入 firmware 子模組
add_subdirectory(${CMAKE_SOURCE_DIR}/../firmware firmware_build)

# 這裡要確保 firmware 的 target 名為 firmware_lib
target_link_libraries(mydb_engine PRIVATE mydb_nvme pthread firmware_lib)

# ────── 7. 編譯提示 ──────
message(STATUS "✅ 完全靜態 (musl) 編譯模式啟用")

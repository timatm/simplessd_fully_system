
cmake_minimum_required(VERSION 3.10)
project(firmware_lib)

set(CMAKE_CXX_STANDARD 17)

# 包含 include/ 目錄的 .hh 檔
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}   
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# 將 firmware 中所有 .cc 檔設為庫的來源
add_library(firmware_lib STATIC
    def.cc
    disk.cc
    IMS_interface.cc
    lbn_pool.cc
    log.cc
    lsmtree.cc
    mapping_table.cc
    persistence.cc
    tree.cc
)

# 方便其他 CMakeLists 使用
target_include_directories(firmware_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    # ${CMAKE_CURRENT_SOURCE_DIR}/../db/src/db_engine
    # ${CMAKE_CURRENT_SOURCE_DIR}/../db/src/db_nvme
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/
        DESTINATION include
        FILES_MATCHING
          PATTERN "*.hh"
        # 需要的话还能排除子目录：
        # PATTERN "internal/*" EXCLUDE
)



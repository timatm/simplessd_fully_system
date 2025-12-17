# cmake_minimum_required(VERSION 3.12)
# project(mydb_static LANGUAGES C CXX)

# # ── 基本設定 ─────────────────────────────────────────────
# set(CMAKE_C_STANDARD 99)
# set(CMAKE_C_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS OFF)
# add_compile_options(-Wall)
# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#     add_compile_definitions(MYDB_DEBUG=1)
# endif()
# # add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
# # add_link_options(-fsanitize=address)

# include(CheckSymbolExists)
# check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
# if(NOT HAVE_ALIGNED_ALLOC)
# 	add_definitions(-DNO_ALIGNED_ALLOC)
# endif()

# set(DB_ENGINE_DIR 	${CMAKE_SOURCE_DIR}/src/db_engine)
# set(DB_NVME_DIR   	${CMAKE_SOURCE_DIR}/src/db_nvme)
# set(IMS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/dev/storage/simplessd/ims/include)
# set(IMS_SRC_DIR 	${CMAKE_CURRENT_SOURCE_DIR}/../src/dev/storage/simplessd/ims/firmware)

# set(NVME_BACKEND "real" CACHE STRING "NVMe backend: real|sim")
# # LBN select policy：
# # 0=WROSTCASE, 1=RR, 2=LEVEL2CH, 3=MYPOLICY
# set(SELECT_POLICY 3 CACHE STRING "LBN select policy: 0=WROSTCASE,1=RR,2=LEVEL2CH,3=MYPOLICY")
# if(NOT SELECT_POLICY MATCHES "^[0-3]$")
#     message(FATAL_ERROR "SELECT_POLICY must be 0,1,2,3 (0=WROSTCASE,1=RR,2=LEVEL2CH,3=MYPOLICY)")
# endif()

# # SStable packing type：
# # 0=kKeyPerPage, 1=kHash, 2=kKeyRange
# set(PACKING_TYPE 0 CACHE STRING "PackingType: 0=kKeyPerPage,1=kHash,2=kKeyRange")

# # 防呆一下，避免亂填
# if(NOT PACKING_TYPE MATCHES "^[0-2]$")
#     message(FATAL_ERROR "PACKING_TYPE must be 0,1,2 (0=kKeyPerPage,1=kHash,2=kKeyRange)")
# endif()

# # 先決定要用哪個實作檔
# if(NVME_BACKEND STREQUAL "real")
#     set(NVME_IMPL ${DB_NVME_DIR}/nvme_simplessd.cc)
# elseif(NVME_BACKEND STREQUAL "sim")
#     set(NVME_IMPL ${DB_NVME_DIR}/nvme_test.cc)
# else()

#     message(FATAL_ERROR "NVME_BACKEND must be 'real' or 'sim'")
# endif()

# # 先創 target
# add_library(mydb_nvme STATIC ${NVME_IMPL})

# # 再根據 backend 打 RUNTYPE
# if(NVME_BACKEND STREQUAL "real")
#     target_compile_definitions(mydb_nvme PUBLIC RUNTYPE=1)
# elseif(NVME_BACKEND STREQUAL "sim")
#     target_compile_definitions(mydb_nvme PUBLIC RUNTYPE=0)
# endif()


# target_compile_definitions(mydb_nvme PUBLIC SELECT_POLICY=${SELECT_POLICY})
# target_compile_definitions(mydb_nvme PUBLIC PACKING_TYPE=${PACKING_TYPE})

# # 只有 real 後端需要 libnvme
# if(NVME_BACKEND STREQUAL "real")
# 	add_library(libnvme STATIC IMPORTED GLOBAL)
# 	set_target_properties(libnvme PROPERTIES
# 		IMPORTED_LOCATION "${DB_NVME_DIR}/lib/libnvme.a"
# 		INTERFACE_INCLUDE_DIRECTORIES "${DB_NVME_DIR}/libnvme"
# 	)
# 	target_link_libraries(mydb_nvme PUBLIC libnvme)
# 	target_include_directories(mydb_nvme PRIVATE
# 		${DB_NVME_DIR}/libnvme
# 		${DB_NVME_DIR}/lib
# 		${DB_NVME_DIR}/libnvme/nvme
# 	)
# endif()

# target_link_libraries(mydb_nvme PUBLIC pthread)
# target_include_directories(mydb_nvme
# 	PUBLIC  ${DB_NVME_DIR} ${CMAKE_SOURCE_DIR}/include ${IMS_INCLUDE_DIR} ${IMS_SRC_DIR}
# 	PRIVATE ${DB_NVME_DIR} ${CMAKE_SOURCE_DIR}/firmware
# )

# # ── mydb_engine：給 YCSB 連的靜態庫（排除 main.cc） ─────
# file(GLOB ENGINE_SRC ${DB_ENGINE_DIR}/*.cc)
# list(FILTER ENGINE_SRC EXCLUDE REGEX ".*/main\\.cc$")


# if(NVME_BACKEND STREQUAL "real")
# 	list(APPEND ENGINE_SRC
# 		${IMS_SRC_DIR}/tree.cc
# 		${IMS_SRC_DIR}/lsmtree.cc
# 		${IMS_SRC_DIR}/def.cc
# 	)
# endif()


# add_library(mydb_engine STATIC ${ENGINE_SRC})
# target_include_directories(mydb_engine
# 	PUBLIC  ${DB_ENGINE_DIR} ${CMAKE_SOURCE_DIR}/include ${IMS_INCLUDE_DIR} ${IMS_SRC_DIR}
# 	PRIVATE ${DB_NVME_DIR}
# )
# # 重要：用 PUBLIC，讓上游（YCSB）只連 mydb_engine 就會自動帶上 mydb_nvme
# target_link_libraries(mydb_engine PUBLIC mydb_nvme)

# # ── firmware 子模組（如需要） ──────────────────────────

# if(NVME_BACKEND STREQUAL "sim")
# 	add_subdirectory(${CMAKE_SOURCE_DIR}/../src/dev/storage/simplessd/ims/firmware firmware_build)
# 	target_compile_definitions(firmware_lib PUBLIC RUNTYPE=0)
# 	target_compile_definitions(firmware_lib PUBLIC
# 		SELECT_POLICY=${SELECT_POLICY}
# 		PACKING_TYPE=${PACKING_TYPE}
# 	)

# 	target_link_libraries(mydb_engine PUBLIC firmware_lib)
# elseif(NVME_BACKEND STREQUAL "real")
# 	# add_subdirectory(${CMAKE_SOURCE_DIR}/../src/dev/storage/simplessd/ims/firmware firmware_build)
# 	# target_compile_definitions(firmware_lib PUBLIC RUNTYPE=0)
# 	# target_compile_definitions(firmware_lib PUBLIC
# 	# 	SELECT_POLICY=${SELECT_POLICY}
# 	# 	PACKING_TYPE=${PACKING_TYPE}
# 	# )

# 	# target_link_libraries(mydb_engine PUBLIC firmware_lib)
# endif()



# # 安裝靜態庫
# install(TARGETS mydb_engine mydb_nvme
#         ARCHIVE DESTINATION lib)


# if(NVME_BACKEND STREQUAL "sim")
# 	install(TARGETS firmware_lib ARCHIVE DESTINATION lib)
# endif()


# # 安裝對外頭檔
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/db_nvme/lib/ DESTINATION lib)
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/../src/dev/storage/simplessd/ims/include/ DESTINATION include)
# message(STATUS "NVME_BACKEND=${NVME_BACKEND}")


cmake_minimum_required(VERSION 3.10)
project(mydb_static LANGUAGES C CXX)

# ── 基本設定 ─────────────────────────────────────────────
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall)

# CMake 3.10 沒有 add_compile_definitions()，改用 add_definitions()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DMYDB_DEBUG=1)
endif()

# ASAN（CMake 3.10 也能用的寫法，若要啟用請取消註解）
# add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
# set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
# set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=address")

include(CheckSymbolExists)
check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
if(NOT HAVE_ALIGNED_ALLOC)
    add_definitions(-DNO_ALIGNED_ALLOC)
endif()

set(DB_ENGINE_DIR   ${CMAKE_SOURCE_DIR}/src/db_engine)
set(DB_NVME_DIR     ${CMAKE_SOURCE_DIR}/src/db_nvme)
set(IMS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/dev/storage/simplessd/ims/include)
set(IMS_SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/../src/dev/storage/simplessd/ims/firmware)

set(NVME_BACKEND "real" CACHE STRING "NVMe backend: real|sim")

# LBN select policy：
# 0=WROSTCASE, 1=RR, 2=LEVEL2CH, 3=MYPOLICY
set(SELECT_POLICY 3 CACHE STRING "LBN select policy: 0=WROSTCASE,1=RR,2=LEVEL2CH,3=MYPOLICY")
if(NOT SELECT_POLICY MATCHES "^[0-3]$")
    message(FATAL_ERROR "SELECT_POLICY must be 0,1,2,3 (0=WROSTCASE,1=RR,2=LEVEL2CH,3=MYPOLICY)")
endif()

# SStable packing type：
# 0=kKeyPerPage, 1=kHash, 2=kKeyRange
set(PACKING_TYPE 0 CACHE STRING "PackingType: 0=kKeyPerPage,1=kHash,2=kKeyRange")
if(NOT PACKING_TYPE MATCHES "^[0-2]$")
    message(FATAL_ERROR "PACKING_TYPE must be 0,1,2 (0=kKeyPerPage,1=kHash,2=kKeyRange)")
endif()

# 先決定要用哪個實作檔
if(NVME_BACKEND STREQUAL "real")
    set(NVME_IMPL ${DB_NVME_DIR}/nvme_simplessd.cc)
elseif(NVME_BACKEND STREQUAL "sim")
    set(NVME_IMPL ${DB_NVME_DIR}/nvme_test.cc)
else()
    message(FATAL_ERROR "NVME_BACKEND must be 'real' or 'sim'")
endif()

# 先創 target
add_library(mydb_nvme STATIC ${NVME_IMPL})

# 再根據 backend 打 RUNTYPE
if(NVME_BACKEND STREQUAL "real")
    target_compile_definitions(mydb_nvme PUBLIC RUNTYPE=1)
elseif(NVME_BACKEND STREQUAL "sim")
    target_compile_definitions(mydb_nvme PUBLIC RUNTYPE=0)
endif()

target_compile_definitions(mydb_nvme PUBLIC SELECT_POLICY=${SELECT_POLICY})
target_compile_definitions(mydb_nvme PUBLIC PACKING_TYPE=${PACKING_TYPE})

# 只有 real 後端需要 libnvme
if(NVME_BACKEND STREQUAL "real")
    add_library(libnvme STATIC IMPORTED GLOBAL)
    set_target_properties(libnvme PROPERTIES
        IMPORTED_LOCATION "${DB_NVME_DIR}/lib/libnvme.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DB_NVME_DIR}/libnvme"
    )
    target_link_libraries(mydb_nvme PUBLIC libnvme)
    target_include_directories(mydb_nvme PRIVATE
        ${DB_NVME_DIR}/libnvme
        ${DB_NVME_DIR}/lib
        ${DB_NVME_DIR}/libnvme/nvme
    )
endif()

target_link_libraries(mydb_nvme PUBLIC pthread)
target_include_directories(mydb_nvme
    PUBLIC  ${DB_NVME_DIR} ${CMAKE_SOURCE_DIR}/include ${IMS_INCLUDE_DIR} ${IMS_SRC_DIR}
    PRIVATE ${DB_NVME_DIR} ${CMAKE_SOURCE_DIR}/firmware
)

# ── mydb_engine：給 YCSB 連的靜態庫（排除 main.cc） ─────
file(GLOB ENGINE_SRC ${DB_ENGINE_DIR}/*.cc)
list(FILTER ENGINE_SRC EXCLUDE REGEX ".*/main\\.cc$")

if(NVME_BACKEND STREQUAL "real")
    list(APPEND ENGINE_SRC
        ${IMS_SRC_DIR}/tree.cc
        ${IMS_SRC_DIR}/lsmtree.cc
        ${IMS_SRC_DIR}/def.cc
    )
endif()

add_library(mydb_engine STATIC ${ENGINE_SRC})
target_include_directories(mydb_engine
    PUBLIC  ${DB_ENGINE_DIR} ${CMAKE_SOURCE_DIR}/include ${IMS_INCLUDE_DIR} ${IMS_SRC_DIR}
    PRIVATE ${DB_NVME_DIR}
)

# 重要：用 PUBLIC，讓上游（YCSB）只連 mydb_engine 就會自動帶上 mydb_nvme
target_link_libraries(mydb_engine PUBLIC mydb_nvme)

# ── firmware 子模組（如需要） ──────────────────────────
if(NVME_BACKEND STREQUAL "sim")
    add_subdirectory(${CMAKE_SOURCE_DIR}/../src/dev/storage/simplessd/ims/firmware firmware_build)
    target_compile_definitions(firmware_lib PUBLIC RUNTYPE=0)
    target_compile_definitions(firmware_lib PUBLIC
        SELECT_POLICY=${SELECT_POLICY}
        PACKING_TYPE=${PACKING_TYPE}
    )
    target_link_libraries(mydb_engine PUBLIC firmware_lib)
elseif(NVME_BACKEND STREQUAL "real")
    # （保留你原本註解掉的 real firmware 部分）
endif()

# 安裝靜態庫
install(TARGETS mydb_engine mydb_nvme
        ARCHIVE DESTINATION lib)

if(NVME_BACKEND STREQUAL "sim")
    install(TARGETS firmware_lib ARCHIVE DESTINATION lib)
endif()

# 安裝對外頭檔
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/db_nvme/lib/ DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/../src/dev/storage/simplessd/ims/include/ DESTINATION include)

message(STATUS "NVME_BACKEND=${NVME_BACKEND}")

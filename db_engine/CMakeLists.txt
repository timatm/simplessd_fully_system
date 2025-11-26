cmake_minimum_required(VERSION 3.12)
project(mydb_static LANGUAGES C CXX)

# ── 基本設定 ─────────────────────────────────────────────
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall)
add_compile_definitions(MYDB_DEBUG)
# add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
# add_link_options(-fsanitize=address)

include(CheckSymbolExists)
check_symbol_exists(aligned_alloc "stdlib.h" HAVE_ALIGNED_ALLOC)
if(NOT HAVE_ALIGNED_ALLOC)
	add_definitions(-DNO_ALIGNED_ALLOC)
endif()

set(DB_ENGINE_DIR ${CMAKE_SOURCE_DIR}/src/db_engine)
set(DB_NVME_DIR   ${CMAKE_SOURCE_DIR}/src/db_nvme)
set(IMS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/dev/storage/simplessd/ims/include)


# 後端選擇：real=libnvme 實機；sim=simplessd 模擬
set(NVME_BACKEND "real" CACHE STRING "NVMe backend: real|sim")
# NVME_DRIVER: 0 = my NVMe driver, 1 = SimpleSSD NVMe driver
set(NVME_DRIVER "1" CACHE STRING "NVMe driver: 0=my driver, 1=SimpleSSD driver")

# ── mydb_nvme：二擇一編入對應後端檔案 ───────────────────
if(NVME_BACKEND STREQUAL "real")
	set(NVME_IMPL ${DB_NVME_DIR}/nvme_simplessd.cc)  # 實機
elseif(NVME_BACKEND STREQUAL "sim")
	set(NVME_IMPL ${DB_NVME_DIR}/nvme_test.cc)       # 模擬（你說的 interface）
else()
  	message(FATAL_ERROR "NVME_BACKEND must be 'real' or 'sim'")
endif()

add_library(mydb_nvme STATIC
  	${NVME_IMPL}
)
target_compile_definitions(mydb_nvme   PUBLIC NVME_DRIVER=${NVME_DRIVER})

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
	PUBLIC  ${DB_NVME_DIR} ${CMAKE_SOURCE_DIR}/include ${IMS_INCLUDE_DIR}
	PRIVATE ${DB_NVME_DIR} ${CMAKE_SOURCE_DIR}/firmware
)

# ── mydb_engine：給 YCSB 連的靜態庫（排除 main.cc） ─────
file(GLOB ENGINE_SRC ${DB_ENGINE_DIR}/*.cc)
list(FILTER ENGINE_SRC EXCLUDE REGEX ".*/main\\.cc$")

add_library(mydb_engine STATIC ${ENGINE_SRC})
target_include_directories(mydb_engine
	PUBLIC  ${DB_ENGINE_DIR} ${CMAKE_SOURCE_DIR}/include ${IMS_INCLUDE_DIR}
	PRIVATE ${DB_NVME_DIR}
)
target_compile_definitions(mydb_engine PUBLIC NVME_DRIVER=${NVME_DRIVER})
# 重要：用 PUBLIC，讓上游（YCSB）只連 mydb_engine 就會自動帶上 mydb_nvme
target_link_libraries(mydb_engine PUBLIC mydb_nvme)

# ── firmware 子模組（如需要） ──────────────────────────
add_subdirectory(${CMAKE_SOURCE_DIR}/../src/dev/storage/simplessd/ims/firmware firmware_build)
target_link_libraries(mydb_engine PRIVATE firmware_lib)

# 安裝靜態庫
install(TARGETS mydb_engine mydb_nvme
        ARCHIVE DESTINATION lib)

install(TARGETS firmware_lib
        ARCHIVE DESTINATION lib)


# 安裝對外頭檔
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/db_nvme/lib/ DESTINATION lib)
message(STATUS "NVME_BACKEND=${NVME_BACKEND}")

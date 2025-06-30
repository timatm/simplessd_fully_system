file(GLOB SRC_FILES *.cc)
list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/testmain.cc)
list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/disktest.cc)
add_library(mapping_obj OBJECT ${SRC_FILES})

# 設定 include path 給其他 target 使用
target_include_directories(mapping_obj PUBLIC ${PROJECT_SOURCE_DIR}/include)

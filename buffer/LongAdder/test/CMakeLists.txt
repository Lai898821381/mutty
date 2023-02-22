project(longadder)

add_definitions(-std=c++14)

include_directories(${PROJECT_SOURCE_DIR})

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)

message(STATUS "CODECODECODE" ${SRC_LIST})

add_executable(longadder ${SRC_LIST})

target_link_libraries(${PROJECT_NAME} pthread)
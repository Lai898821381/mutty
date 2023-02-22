project(buffer)

add_definitions(-std=c++17)

# SET(CMAKE_BUILD_TYPE "Debug")

include_directories(${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/LongAdder)

aux_source_directory(${PROJECT_SOURCE_DIR} SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/LongAdder LONGADDER_LIST)

message(STATUS "CODECODECODE" ${SRC_LIST})
message(STATUS "CODECODECODE" ${LONGADDER_LIST})

add_executable(buffer ${SRC_LIST} ${LONGADDER_LIST} ${BASE_LIST})

target_link_libraries(${PROJECT_NAME} pthread)
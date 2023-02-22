project(timer)
  
cmake_minimum_required(VERSION 3.16)

add_definitions(--std=c++17)

include_directories(${PROJECT_SOURCE_DIR} delay_queue thread_pool)

aux_source_directory(. SRC_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/delay_queue DELAYQUEUE_LIST)
aux_source_directory(${PROJECT_SOURCE_DIR}/thread_pool THREADPOOL_LIST)

add_executable(timer ${SRC_LIST} ${DELAYQUEUE_LIST} ${THREADPOOL_LIST})

target_link_libraries(${PROJECT_NAME} pthread)
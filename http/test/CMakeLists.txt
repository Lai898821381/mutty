project(mutty_httpserver)

add_definitions(-std=c++17)

include_directories(.. ../../buffer ../../base ../../buffer/LongAdder ../../timer/delay_queue ../../timer/thread_pool ../../timer ../..)

aux_source_directory(.. SRC_LIST)
aux_source_directory(../../buffer BUFFER_LIST)
list(REMOVE_ITEM BUFFER_LIST "../../buffer/test.cpp")
aux_source_directory(../../buffer/LongAdder LONGADDER_LIST)
aux_source_directory(../../base BASE_LIST)
aux_source_directory(../../timer TIMER_LIST)
aux_source_directory(../../timer/thread_pool THREADPOOL_LIST)
aux_source_directory(../../timer/delay_queue DELAYQUEUE_LIST)
aux_source_directory(../.. TCPSERVER_LIST)


message(STATUS "CODECODECODE" ${SRC_LIST})
message(STATUS "CODECODECODE" ${BUFFER_LIST})

add_executable(${PROJECT_NAME}  HttpServerTest.cpp ${SRC_LIST}
                ${BUFFER_LIST} ${BASE_LIST} ${LONGADDER_LIST} ${TIMER_LIST} ${THREADPOOL_LIST} ${DELAYQUEUE_LIST} ${TCPSERVER_LIST} )

target_link_libraries(${PROJECT_NAME} pthread)
cmake_minimum_required(VERSION 2.6)
add_definitions(-Wall -W -ansi)

execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/error_documents
                ${CMAKE_BINARY_DIR}/error_documents)

add_executable(httpd httpd.c)

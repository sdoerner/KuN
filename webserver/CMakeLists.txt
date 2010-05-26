cmake_minimum_required(VERSION 2.6)
add_definitions(-Wall -W -ansi)
set(CMAKE_C_FLAGS_DEBUG -g)

execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/error_documents
                ${CMAKE_BINARY_DIR}/error_documents)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)

add_library(log log.c)
add_executable(httpd httpd.c log.h)
target_link_libraries (httpd log)

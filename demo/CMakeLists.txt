cmake_minimum_required(VERSION 2.8)

project(LogClient)

add_executable(${PROJECT_NAME} "main.c" "../Log.h" "../Log.c")
find_package(Threads)
target_link_libraries (${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

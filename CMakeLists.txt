  cmake_minimum_required(VERSION 3.5)

project(programmingassignment2)
include_directories(.)

add_executable(main
        main.c)

add_executable(manager_send
        manager_send.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(main Threads::Threads)




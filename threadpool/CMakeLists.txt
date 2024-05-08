cmake_minimum_required(VERSION 3.28)
project(threadpool)

set(CMAKE_CXX_STANDARD 17)

add_executable(threadpool main.cpp
        Task.cpp
        ThreadPool.cpp
        )

cmake_minimum_required(VERSION 3.28)
project(shmqueue)

set(CMAKE_CXX_STANDARD 17)

add_executable(shmqueue main.cpp
        shmmqueue.h
        shm_rwlock.h
        shm_rwlock.cpp
        shmmqueue.cpp)

cmake_minimum_required(VERSION 3.28)
project(WebServer)

set(CMAKE_CXX_STANDARD 17)

add_executable(WebServer main.cpp
        pub.h
        pub.cpp)

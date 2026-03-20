cmake_minimum_required(VERSION 3.10)
project(MemoryDumper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
        AVML.cpp
)

add_executable(test main_avml.cpp)
add_executable(AVML AVML.cpp)
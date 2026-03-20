cmake_minimum_required(VERSION 3.10)
project(MemoryDumper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/file_io.cpp
    src/parsers.cpp
    src/copier.cpp
    src/strategies.cpp
    src/manager.cpp
        include/avml/interfaces/manager_interface.h
        include/avml/interfaces/strategy_interface.h
)

add_executable(test main_avml.cpp)
add_executable(AVML
        ${SOURCES}
        src/main.cpp
)

target_include_directories(AVML PUBLIC include)
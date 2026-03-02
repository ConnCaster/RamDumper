cmake_minimum_required(VERSION 3.10)
project(MemoryDumper VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Исходники
set(SOURCES
        src/kcore_strategy.cpp
        src/crash_strategy.cpp
        src/dump_factory.cpp
        src/memory_dumper.cpp
        main.cpp
)

# Заголовки
include_directories(include)

# Исполняемый файл
add_executable(MemoryDumper ${SOURCES})

add_executable(test main_avml.cpp)
add_executable(AVML AVML.cpp)
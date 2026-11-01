cmake_minimum_required(VERSION 3.16)
project(lr2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(main
    src/main.cpp
    src/cli.cpp
    src/codec.cpp
    src/file_io.cpp
)

target_include_directories(main PRIVATE include)

find_library(UUID_LIB uuid)
target_link_libraries(main PRIVATE ${UUID_LIB})

target_compile_options(main PRIVATE -Wall -Wextra)
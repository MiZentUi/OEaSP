cmake_minimum_required(VERSION 3.16)
project(lr4 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(STUDENT_ID "103" CACHE STRING "Student ID")

add_executable(lr4
    src/main.c
    src/cli.c
    src/shared.c
    src/model.c
    src/manager.c
)

target_include_directories(lr4 PRIVATE include)
target_compile_definitions(lr4 PRIVATE
    STUDENT_ID=${STUDENT_ID}
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
)
target_link_libraries(lr4 PRIVATE rt)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lr4 PRIVATE -Wall -Wextra -Wpedantic)
endif()

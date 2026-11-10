cmake_minimum_required(VERSION 3.16)
project(lr5 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(STUDENT_ID "103" CACHE STRING "Student ID")
math(EXPR DEFAULT_PORT "20000 + ${STUDENT_ID}")

add_library(lr5_common STATIC
    src/common/timing.c
    src/common/logging.c
    src/common/net.c
)

target_include_directories(lr5_common PUBLIC include)
target_compile_definitions(lr5_common PUBLIC
    STUDENT_ID=${STUDENT_ID}
    DEFAULT_PORT=${DEFAULT_PORT}
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lr5_common PRIVATE -Wall -Wextra -Wpedantic)
endif()

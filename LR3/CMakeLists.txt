cmake_minimum_required(VERSION 3.16)
project(lr3 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(STUDENT_ID "103" CACHE STRING "Student ID")

add_executable(logger
    src/main.c
    src/manager.c
    src/process.c
    src/server.c
    src/server_handlers.c
    src/client.c
    src/bench_client.c
    src/control.c
    src/logging.c
    src/transport.c
)

target_include_directories(logger PRIVATE include)
target_compile_definitions(logger PRIVATE
    STUDENT_ID=${STUDENT_ID}
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(logger PRIVATE -Wall -Wextra -Wpedantic)
endif()

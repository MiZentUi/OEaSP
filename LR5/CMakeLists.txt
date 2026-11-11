cmake_minimum_required(VERSION 3.16)
project(lr5 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(STUDENT_ID "103" CACHE STRING "Student ID")
math(EXPR DEFAULT_PORT "20000 + ${STUDENT_ID}")

add_library(common STATIC
    src/common/timing.c
    src/common/logging.c
    src/common/net.c
    src/common/http.c
)

target_include_directories(common PUBLIC include)
target_compile_definitions(common PUBLIC
    STUDENT_ID=${STUDENT_ID}
    DEFAULT_PORT=${DEFAULT_PORT}
    _POSIX_C_SOURCE=200809L
    _DEFAULT_SOURCE
)

add_executable(client
    src/client/main.c
    src/client/cli.c
    src/client/client.c
)

add_executable(server
    src/server/main.c
    src/server/cli.c
    src/server/server.c
)

target_link_libraries(client PRIVATE common)
target_link_libraries(server PRIVATE common)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(client PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
endif()

cmake_minimum_required(VERSION 3.20)
project(jsync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(spdlog REQUIRED)

add_library(jsync_core
    src/file_ops.cpp
    src/sha1.cpp
    src/peer.cpp
    src/wire.cpp
)
target_include_directories(jsync_core PUBLIC src)
target_link_libraries(jsync_core PUBLIC spdlog::spdlog)
target_compile_options(jsync_core PRIVATE -Wall -Wextra)

add_executable(jsync src/main.cpp)
target_link_libraries(jsync PRIVATE jsync_core)
target_compile_options(jsync PRIVATE -Wall -Wextra)

enable_testing()
add_subdirectory(tests)

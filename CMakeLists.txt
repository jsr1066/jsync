cmake_minimum_required(VERSION 3.20)
project(jsync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)

# Standalone Asio is header-only; Homebrew ships no CMake package for it.
find_path(ASIO_INCLUDE_DIR asio.hpp REQUIRED)
add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${ASIO_INCLUDE_DIR})
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
target_link_libraries(asio INTERFACE Threads::Threads)

add_library(jsync_core
    src/file_ops.cpp
    src/sha1.cpp
    src/peer.cpp
)
target_include_directories(jsync_core PUBLIC src)
target_link_libraries(jsync_core PUBLIC spdlog::spdlog asio)
target_compile_options(jsync_core PRIVATE -Wall -Wextra)

enable_testing()
add_subdirectory(tests)

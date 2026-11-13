cmake_minimum_required(VERSION 3.20)

project(CairnShop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

enable_testing()

find_package(Threads REQUIRED)

# Vendored CairnKV client (v1.0.0): client + wire protocol only.
add_subdirectory(third_party/cairnkv EXCLUDE_FROM_ALL)

add_subdirectory(backend)

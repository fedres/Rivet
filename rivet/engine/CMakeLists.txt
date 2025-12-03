cmake_minimum_required(VERSION 3.20)
project(rivet-engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rivet-engine STATIC
    src/engine.cpp
)

target_include_directories(rivet-engine PUBLIC include)

cmake_minimum_required(VERSION 3.0)
project(final)

set(CMAKE_CXX_STANDARD 11)

add_executable(final main.cpp)
target_link_libraries(final pthread)

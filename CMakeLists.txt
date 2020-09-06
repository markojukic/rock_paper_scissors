cmake_minimum_required(VERSION 3.16)
project(rock_paper_scissors)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_subdirectory("src")
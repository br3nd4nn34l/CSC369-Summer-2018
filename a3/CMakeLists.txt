cmake_minimum_required(VERSION 3.6)
project(a3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    ext2.h
    tmp.c
    ext2_helper.h
    list.h
    ext2_ls.c)

add_executable(a3 ${SOURCE_FILES})
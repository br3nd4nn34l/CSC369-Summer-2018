cmake_minimum_required(VERSION 3.6)
project(starter)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    traceprogs/blocked.c
    traceprogs/matmul.c
    traceprogs/simpleloop.c
    traceprogs/timer.h
    clock.c
    fifo.c
    lru.c
    opt.c
    pagetable.c
    pagetable.h
    rand.c
    sim.c
    sim.h
    swap.c)

add_executable(starter ${SOURCE_FILES})
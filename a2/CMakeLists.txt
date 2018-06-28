cmake_minimum_required(VERSION 3.6)
project(a2)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    starter/traceprogs/blocked.c
    starter/traceprogs/matmul.c
    starter/traceprogs/simpleloop.c
    starter/traceprogs/timer.h
    starter/clock.c
    starter/CMakeLists.txt
    starter/fifo.c
    starter/lru.c
    starter/Makefile
    starter/opt.c
    starter/pagetable.c
    starter/pagetable.h
    starter/rand.c
    starter/sim.c
    starter/sim.h
    starter/swap.c)

add_executable(a2 ${SOURCE_FILES})
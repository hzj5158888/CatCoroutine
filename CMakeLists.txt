cmake_minimum_required(VERSION 3.5.0)
project(Coroutine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_BUILD_TYPE "Debug")

# root
set(SRC .)
include_directories(${SRC}/include)
aux_source_directory(${SRC} main)

# data_structure
set(DS_PATH ${SRC}/data_structure)
include_directories(${DS_PATH}/include)
aux_source_directory(${DS_PATH} data_structure)

# allocator
set(ALLOCATOR_PATH ${SRC}/allocator)
include_directories(${ALLOCATOR_PATH}/include)
aux_source_directory(${ALLOCATOR_PATH} allocator)

# context
set(CONTEXT_PATH ${SRC}/context)
include_directories(${CONTEXT_PATH}/include)
aux_source_directory(${CONTEXT_PATH} context)
set(context ${context} ${CONTEXT_PATH}/switch_context_first_run.asm)
set(context ${context} ${CONTEXT_PATH}/_switch_context.asm)
set(context ${context} ${CONTEXT_PATH}/save_context.asm)

# utils
set(UTILS_PATH ${SRC}/utils)
include_directories(${UTILS_PATH}/include)
aux_source_directory(${UTILS_PATH} utils)

# sched
set(SCHED_PATH ${SRC}/sched)
include_directories(${SCHED_PATH}/include)
aux_source_directory(${SCHED_PATH} sched)

# sync
set(SYNC_PATH ${SRC}/sync)
include_directories(${SYNC_PATH}/include)
aux_source_directory(${SYNC_PATH} sync)

# test
set(TEST_PATH ${SRC}/test)
include_directories(${TEST_PATH}/include)
aux_source_directory(${TEST_PATH} test)

# exec
enable_language(ASM-ATT)
set(CMAKE_CXX_COMPILER /usr/bin/g++-13)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -D_FORTIFY_SOURCE=0")
add_executable(Coroutine ${main} ${data_structure} ${allocator} ${context} ${utils} ${sched} ${sync} ${test})
add_compile_definitions(__CPU_CORE__=12)

# pthread
target_link_libraries(Coroutine pthread)
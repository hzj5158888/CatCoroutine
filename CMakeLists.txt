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
set(context ${context} ${CONTEXT_PATH}/switch_context_first_run.S)
set(context ${context} ${CONTEXT_PATH}/_switch_context.S)
set(context ${context} ${CONTEXT_PATH}/save_context.S)
set(context ${context} ${CONTEXT_PATH}/swap_context_impl.S)

# utils
set(UTILS_PATH ${SRC}/utils)
include_directories(${UTILS_PATH}/include)
aux_source_directory(${UTILS_PATH} utils)
set(utils ${utils} ${UTILS_PATH}/include/utils.S)

# sched
set(SCHED_PATH ${SRC}/sched)
include_directories(${SCHED_PATH}/include)
aux_source_directory(${SCHED_PATH} sched)

# sync
set(SYNC_PATH ${SRC}/sync)
include_directories(${SYNC_PATH}/include)
aux_source_directory(${SYNC_PATH} sync)

# timer
set(TIMER_PATH ${SRC}/timer)
include_directories(${TIMER_PATH}/include)
aux_source_directory(${TIMER_PATH} timer)

# io
set(IO_PATH ${SRC}/io)
include_directories(${IO_PATH}/include)
aux_source_directory(${IO_PATH} io)

# test
set(TEST_PATH ${SRC}/test)
include_directories(${TEST_PATH}/include)
aux_source_directory(${TEST_PATH} test)

# boost
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# exec
enable_language(ASM)
set(CMAKE_CXX_COMPILER /usr/bin/g++-13)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -Wall -Werror -fno-omit-frame-pointer")
add_executable(Coroutine ${main} ${data_structure} ${allocator} ${context} ${utils} ${sched} ${sync} ${test} ${timer} ${io})
# pthread boost
target_link_libraries(Coroutine pthread ${Boost_LIBRARIES})

# basic configuration
add_compile_definitions(__CPU_CORE__=6)
add_compile_definitions(__CACHE_LINE__=64)

# debug flag
#add_compile_definitions(__DEBUG__)
#add_compile_definitions(__DEBUG_SEM_TRACE__)
#add_compile_definitions(__DEBUG_SCHED__)
#add_compile_definitions(__DEBUG_SCHED_RUN__)
#add_compile_definitions(__DEBUG_SCHED_READY__)

# memory pool
#add_compile_definitions(__MEM_PMR__)

# Recycler lock free
#add_compile_definitions(__RECYCLER_LF__)

# scheduler data structure
add_compile_definitions(__SCHED_CFS__)
#add_compile_definitions(__SCHED_FIFO__)

# stack allocate mode
add_compile_definitions(__STACK_DYN__)
add_compile_definitions(__STACK_DYN_MMAP__)
#add_compile_definitions(__STACK_STATIC__)

# context
add_compile_definitions(__CONTEXT_STACK_PROTECTOR__)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-all")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector")
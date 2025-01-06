cmake_minimum_required(VERSION 3.5.0)
project(Coroutine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_BUILD_TYPE "Debug")

# root
set(SRC .)
include_directories(${SRC}/include)
aux_source_directory(${SRC} main)

# mem_pool
set(MEM_POOL_PATH ${SRC}/mem_pool)
include_directories(${MEM_POOL_PATH}/include)
aux_source_directory(${MEM_POOL_PATH} mem_pool)

# context
set(CONTEXT_PATH ${SRC}/context)
include_directories(${CONTEXT_PATH})
aux_source_directory(${CONTEXT_PATH} context)

# exec
add_compile_options(-std=c++17 -Wall -g)
add_executable(Coroutine ${main} ${mem_pool} ${context})

# pthread
target_link_libraries(Coroutine pthread)
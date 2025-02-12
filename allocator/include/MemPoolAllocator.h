#pragma once

#include "MemoryPool.h"

template<typename T>
class MemPoolAllocator
{
private:
    MemoryPool pool{};
public:
    T * allocate(size_t size)
    {
        return reinterpret_cast<T*>(pool.allocate(size));
    }

    void deallocate(T * ptr, size_t size)
    {
        pool.deallocate(ptr);
    }
};
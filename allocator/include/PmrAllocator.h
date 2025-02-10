#pragma once

#include <cstddef>
#include <memory_resource>

#include "../../utils/include/utils.h"

template<typename T>
class PmrAllocatorLockFree
{
private:
    std::pmr::synchronized_pool_resource pool{get_default_pmr_opt()};
public:
    PmrAllocatorLockFree() = default;

    T * allocate(size_t size)
    {
        return reinterpret_cast<T*>(pool.allocate(size));
    }

    void deallocate(T * ptr, size_t size)
    {
        pool.deallocate(ptr, size);
    }
};
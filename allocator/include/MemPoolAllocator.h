#pragma once

#include "MemoryPool.h"
#include "../include/CoCtx.h"

namespace co {
    template<typename T>
    class MemPoolAllocator
    {
    private:
        template<class U>
        friend class MemPoolAllocator;
    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        MemPoolAllocator() = default;
        ~MemPoolAllocator() = default;

        template <typename U>
        explicit MemPoolAllocator(const MemPoolAllocator<U>&) noexcept {}

        T * allocate(size_type size)
        {
            return reinterpret_cast<T *>(co_ctx::g_alloc->oth_pool.allocate(size));
        }

        void deallocate(T * ptr, size_type size)
        {
            co_ctx::g_alloc->oth_pool.deallocate(ptr, size);
        }
    };
}
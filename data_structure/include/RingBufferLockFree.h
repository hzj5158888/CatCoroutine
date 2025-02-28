#pragma once

#include <cstddef>
#include <vector>
#include <cassert>

#include "../../include/CoCtx.h"
#include "../../utils/include/atomic_utils.h"

namespace co {
    template<typename T, typename Allocator = std::allocator<T>>
    class RingBufferLockFree
    {
    private:
        T * m_data{};
        size_t m_capacity{1};
        std::atomic<size_t> m_write{}, m_write_fence{};
        std::atomic<size_t> m_read{}, m_read_fence{};

        constexpr size_t mod_idx(size_t idx) { return idx % (m_capacity); }

    public:
        RingBufferLockFree() { m_data = Allocator{}.allocate(sizeof(T) * 1); };
        explicit RingBufferLockFree(size_t capacity)
        {
            m_capacity = capacity;
            m_write_fence = capacity;
            m_data = Allocator{}.allocate(sizeof(T) * m_capacity);
        }

        ~RingBufferLockFree()
        {
            auto cur_read = m_read.load(std::memory_order_acquire);
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (; cur_read != m_read_fence; cur_read++)
                    m_data[mod_idx(cur_read)].~T();
            }

            Allocator{}.deallocate(m_data, sizeof(T) * m_capacity);
        }

        template<typename V>
        bool push(V && x)
        {
            size_t cur_write_fence{};
            size_t cur_write = m_write.load(std::memory_order_acquire);
            do {
                cur_write_fence = m_write_fence.load(std::memory_order_acquire);
                if (cur_write == cur_write_fence)
                    return false;

            } while (!m_write.compare_exchange_weak(cur_write, cur_write + 1));

            m_data[mod_idx(cur_write)] = std::forward<V>(x);
            m_read_fence++;
            return true;
        }

        template<typename ... Args>
        bool emplace(Args &&... args)
        {
            size_t cur_write_fence{};
            size_t cur_write = m_write.load(std::memory_order_acquire);
            do {
                cur_write_fence = m_write_fence.load(std::memory_order_acquire);
                if (cur_write == cur_write_fence)
                    return false;

            } while (!m_write.compare_exchange_weak(cur_write, cur_write + 1));

            new (std::addressof(m_data[mod_idx(cur_write)])) T(std::forward<Args>(args)...);
            m_read_fence++;
            return true;
        }

        bool pop(T & x)
        {
            size_t cur_read = m_read.load(std::memory_order_acquire);
            size_t cur_read_fence{};
            do {
                cur_read_fence = m_read_fence.load(std::memory_order_relaxed);
                if (cur_read == cur_read_fence)
                    return false;

            } while (!m_read.compare_exchange_weak(cur_read, cur_read + 1));

            x = std::move(m_data[mod_idx(cur_read)]);
            if constexpr (!std::is_trivially_destructible_v<T>)
                m_data[mod_idx(cur_read)].~T();

            m_write_fence++;
            return true;
        }
    };
}
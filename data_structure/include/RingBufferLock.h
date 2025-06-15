//
// Created by hzj on 25-1-10.
//

#pragma once

#include <utility>
#include <array>
#include <cassert>
#include <vector>

#include "../../utils/include/utils.h"
#include "../../utils/include/spin_lock_sleep.h"

namespace co {
    template<typename T, std::size_t N, typename Allocator = std::allocator<T>>
    class RingBufferLock {
    private:
        T * m_data{};
        size_t m_size{};
        size_t rear{}, front{};
        spin_lock_sleep m_lock{};
        static_assert(N > 0);

        constexpr size_t mod_idx(size_t idx)
        {
            if constexpr (is_pow_of_2(N))
                return idx & (N - 1);
            else
                return idx % N;
        }

        std::atomic<size_t> * get_size() { return reinterpret_cast<std::atomic<size_t>*>(&m_size); }
    public:
        RingBufferLock() { m_data = Allocator{}.allocate(sizeof(T) * N); };

        RingBufferLock(RingBufferLock && buf) noexcept { swap(std::move(buf)); }

        ~RingBufferLock()
        {
            std::lock_guard lock(m_lock);
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (; m_size > 0; m_size--)
                {
                    m_data[front].~T();
                    front = mod_idx(front + 1);
                }
            }

            Allocator{}.deallocate(m_data, sizeof(T) * N);
        }

        void swap(RingBufferLock && buf)
        {
            std::swap(m_data, buf.m_data);
            std::swap(rear, buf.rear);
            std::swap(front, buf.front);
            std::swap(m_size, buf.m_size);
        }

        [[nodiscard]] bool empty()
        {
            return size() == 0;
        }

        [[nodiscard]] std::size_t size()
        {
            return get_size()->load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool full()
        {
            return size() == N;
        }

        template<class V>
        bool push(V && x)
        {
            std::lock_guard lock(m_lock);
            if (UNLIKELY(full()))
                return false;

            m_data[rear++] = std::forward<V>(x);
            rear = mod_idx(rear);
            m_size++;
            return true;
        }

        template<class ... Args>
        bool emplace(Args &&... args)
        {
            std::lock_guard lock(m_lock);

            if (UNLIKELY(full()))
                return false;

            new (std::addressof(m_data[rear++])) T{std::forward<Args>(args)...};
            rear = mod_idx(rear);
            m_size++;
            return true;
        }

        bool pop(T & ans)
        {
            std::lock_guard lock(m_lock);
            if (UNLIKELY(empty()))
                return false;

            auto tmp = front++;
            front = mod_idx(front);
            ans = std::move(m_data[tmp]);
            if constexpr (!std::is_trivially_destructible_v<T>)
                m_data[tmp].~T();

            m_size--;
            return true;
        }
    };
}

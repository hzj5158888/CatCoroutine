//
// Created by hzj on 25-1-10.
//

#ifndef COROUTINE_RINGBUFFER_H
#define COROUTINE_RINGBUFFER_H

#include <utility>
#include <array>
#include <cassert>
#include <vector>

namespace co {
    template<typename T, std::size_t N, typename Allocator = std::allocator<T>>
    class RingBuffer {
    private:
        T * m_data{};
        std::size_t m_size{};
        std::size_t rear{}, front{};
        static_assert(N > 0);

        constexpr size_t mod_idx(size_t idx)
        {
            if constexpr (is_pow_of_2(N))
                return idx & (N - 1);
            else
                return idx % N;
        }

    public:
        RingBuffer() { m_data = Allocator{}.allocate(sizeof(T) * N); };

        RingBuffer(RingBuffer && buf) noexcept { swap(std::move(buf)); }

        ~RingBuffer()
        {
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

        void swap(RingBuffer && buf)
        {
            std::swap(m_data, buf.m_data);
            std::swap(rear, buf.rear);
            std::swap(front, buf.front);
            std::swap(m_size, buf.m_size);
        }

        [[nodiscard]] bool empty() const
        {
            return m_size == 0;
        }

        [[nodiscard]] std::size_t size() const
        {
            return m_size;
        }

        [[nodiscard]] bool full() const
        {
            return m_size == N;
        }

        template<class V>
        bool push(V && x)
        {
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
            if (UNLIKELY(full()))
                return false;

            new (std::addressof(m_data[rear++])) T{std::forward<Args>(args)...};
            rear = mod_idx(rear);
            m_size++;
            return true;
        }

        bool pop(T & ans)
        {
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


#endif //COROUTINE_RINGBUFFER_H

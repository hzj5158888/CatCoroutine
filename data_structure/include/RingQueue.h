#pragma once

#include <vector>
#include <cstring>
#include <cmath>

namespace co {
    template<typename T, typename Alloc = std::allocator<T>>
    class RingQueue
    {
    private:
        constexpr static auto DEFAULT_SIZE = 128;
        constexpr static auto GROW_FACTOR = 4;

        T * m_data{};
        size_t m_capacity{};
        size_t m_front{}, m_back{};
        size_t m_size{};

        bool full()
        {
            return size() == m_capacity;
        }

        void make_ring_by_move_front()
        {
            int64_t new_end = m_capacity - 1;
            int64_t old_end = m_size - 1;

            auto move_dis = new_end - old_end;
            auto move_cnt = old_end - m_front + 1;
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                /* memmove 允许重叠地址区域 */
                std::memmove(&m_data[m_front + move_dis], &m_data[m_front], move_cnt * sizeof(T));
            } else {
                for (size_t i = 0; i < move_cnt; i++)
                {
                    std::swap(m_data[new_end], m_data[old_end]);
                    old_end--;
                    new_end--;
                }
            }

            m_front += move_dis;
        }

        void make_ring_by_move_back()
        {
            int64_t new_end = m_capacity - 1;
            int64_t old_end = m_size - 1;

            if (m_back == 0)
            {
                m_back = old_end + 1;
                return;
            }

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                auto move_dis = std::min((size_t)new_end - old_end, m_back);
                std::memcpy(&m_data[old_end + 1], &m_data[0], move_dis * sizeof(T));
                std::memmove(&m_data[0], &m_data[move_dis], (m_back - move_dis) * sizeof(T));
            } else {
                old_end = mod_idx(old_end + 1);
                for (size_t i = 0; i < m_back; i++)
                {
                    std::swap(m_data[old_end], m_data[i]);
                    old_end = mod_idx(old_end + 1);
                }
            }

            m_back = mod_idx((m_size - 1) + m_back + 1);
        }

        void extend()
        {
            T * new_mem = Alloc{}.allocate(sizeof(T) * GROW_FACTOR * m_capacity);

            size_t begin = 0;
            size_t size_first = m_size - m_front;
            std::memcpy((void*)&new_mem[begin], (void*)&m_data[m_front], sizeof(T) * size_first);
            begin += size_first;

            size_t size_second = m_back;
            std::memcpy((void*)&new_mem[begin], (void*)&m_data[0], sizeof(T) * size_second);

            Alloc{}.deallocate(m_data, m_capacity * sizeof(T));
            m_capacity *= GROW_FACTOR;
            m_data = new_mem;

            m_front = 0;
            m_back = m_size;
        }

        template<class F>
        constexpr F mod_idx(F idx)
        {
            if constexpr (std::is_unsigned_v<F>)
                return idx & (m_capacity - 1);
            else
                return (idx % m_capacity + m_capacity) % m_capacity;
        }

        template<class F>
        constexpr F mod_idx(F idx, size_t M)
        {
            if constexpr (std::is_unsigned_v<F>)
                return idx & (M - 1);
            else
                return (idx % M + M) % M;
        }
    public:
        RingQueue()
        {
            m_data = Alloc{}.allocate(sizeof(T) * DEFAULT_SIZE);
            m_capacity = DEFAULT_SIZE;
        }

        explicit RingQueue(size_t size)
        {
            assert(size > 0);
            size = ceil_pow_2(size);
            m_data = Alloc{}.allocate(sizeof(T) * size);
            m_capacity = size;
        }

        bool front(T & x)
        {
            if (empty())
                return false;

            x = m_data[m_front];
            return true;
        }

        template<class F>
        void push(F && x)
        {
            if (full())
                extend();

            auto cur_idx = m_back;
            m_back = mod_idx(m_back + 1);
            m_data[cur_idx] = std::forward<F>(x);
            m_size++;
        }

        void push_all(const std::vector<T> & vec)
        {
            for (auto & v : vec)
                push(v);
        }

        bool pop(T & x)
        {
            if (empty())
                return false;

            auto cur_idx = m_front;
            m_front = mod_idx(m_front + 1);

            if constexpr (std::is_trivially_copyable_v<T>)
                x = m_data[cur_idx];
            else
                x = std::move(m_data[cur_idx]);

            if constexpr (!std::is_trivially_destructible_v<T>)
                m_data[cur_idx].~T();

            m_size--;
            return true;
        }

        bool pop()
        {
            if (empty())
                return false;

            auto cur_idx = m_front;
            m_front = mod_idx(m_front + 1);
            if constexpr (!std::is_trivially_destructible_v<T>)
                m_data[cur_idx].~T();

            m_size--;
            return true;
        }

        [[nodiscard]] size_t size() const { return m_size; }

        bool empty() { return size() == 0; }
    };
}
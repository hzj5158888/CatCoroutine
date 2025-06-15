#pragma once

#include <cstdint>
#include <vector>

#include "../../utils/include/utils.h"

namespace co {
    template<typename T, typename CMP = std::less<T>>
    class BinaryHeap
    {
    private:
        constexpr inline int64_t get_fa(int64_t son) { return son >> 1; }

        constexpr inline int64_t get_son(int64_t u, int x) { return (u << 1) + x; }

        void pushup(int64_t u)
        {
            while (true)
            {
                auto fa = get_fa(u);
                if (UNLIKELY(!fa) || m_cmp(m_data[fa], m_data[u]))
                {
                    break;
                } else {
                    std::swap(m_data[fa], m_data[u]);
                    u = fa;
                }

            }
        }

        void pushdown(int64_t u)
        {
            auto cur_size = (int64_t) m_data.size();
            while (true)
            {
                int64_t son = get_son(u, 0);
                if (UNLIKELY(son > (cur_size >> 1)))
                    break;

                for (int i = 1; i < 2; i++)
                {
                    int64_t cur_son = get_son(u, i);
                    if (UNLIKELY(cur_son > (cur_size >> 1)))
                        break;

                    if (m_cmp(m_data[cur_son], m_data[son]))
                        son = cur_son;
                }

                if (m_cmp(m_data[u], m_data[son]))
                    break;

                std::swap(m_data[u], m_data[son]);
                u = son;
            }
        }

        static bool default_gc_strategy(size_t capacity, size_t size)
        {
            constexpr static auto MAX_CAPACITY_GAP = 4;
            constexpr static auto VECTOR_GROW_STEP = 2;
            constexpr static auto MIN_GC_CAPACITY = 8192;

            return capacity < MIN_GC_CAPACITY || capacity / size < std::pow(VECTOR_GROW_STEP, MAX_CAPACITY_GAP);
        }

        std::vector<T> m_data{1};
        CMP m_cmp{};
    public:
        void pop()
        {
            DASSERT(!empty());
            std::swap(m_data[1], m_data.back());
            m_data.pop_back();
            if (LIKELY(!empty()))
                pushdown(1);
        }

        template<typename F>
        void push(F && data)
        {
            m_data.push_back(std::forward<F>(data));
            pushup(m_data.size() - 1);
        }

        template<typename F>
        void replace_top(F && data)
        {
            DASSERT(!empty());
            m_data[1] = std::forward<F>(data);
            pushdown(1);
        }

        template<typename F>
        void push_all(const std::vector<F> & data)
        {
            if (UNLIKELY(data.empty()))
                return;

            auto start_idx = (int64_t) m_data.size();
            m_data.insert(m_data.end(), data.begin(), data.end());
            for (int64_t i = start_idx; i < (int64_t) (m_data.size() >> 1); i++)
                pushup(i);
        }

        T top() const
        {
            DASSERT(!empty());
            return m_data[1];
        }

        template<class F>
        bool gc(F && should_we_gc = default_gc_strategy)
        {
            static_assert(std::is_invocable_r_v<bool, F, size_t, size_t>);
            static_assert(std::is_invocable_r_v<bool, F, size_t, size_t, size_t>);

            auto capacity = m_data.capacity();
            auto size = m_data.size();
            if constexpr (std::is_invocable_r_v<bool, F, size_t, size_t>)
            {
                if (!should_we_gc(capacity, size))
                    return false;
            } else {
                if (!should_we_gc(capacity, size, sizeof(T)))
                    return false;
            }

            m_data.shrink_to_fit();
            return true;
        }

        [[nodiscard]] int64_t size() const { return (int64_t) m_data.size() - 1; }

        [[nodiscard]] bool empty() const { return size() <= 0; }
    };
}
#pragma once

#include <utility>
#include <vector>

#include "../../utils/include/utils.h"

namespace co {
    template<typename T, typename CMP = std::less<T>>
    class QuaternaryHeap {
    private:
        /* x := [0, 3] */
        constexpr inline int64_t get_son(int64_t fa, int x) { return ((fa - 1) << 2) + 2 + x; }

        constexpr inline int64_t get_fa(int64_t son) { return (son + 2) >> 2; }

        void pushup(int64_t u)
        {
            while (true)
            {
                auto fa = get_fa(u);
                //mm_prefetch(&m_data[fa], 1, 3);
                if (LIKELY(fa) && !m_cmp(m_data[fa], m_data[u]))
                {
                    std::swap(m_data[fa], m_data[u]);
                    u = fa;
                } else {
                    break;
                }
            }
        }

        void pushdown(int64_t u)
        {
            auto cur_size = (int64_t) m_data.size();
            while (true)
            {
                int64_t son = get_son(u, 0);
                if (UNLIKELY(son > (cur_size >> 2)))
                    break;

                int64_t min_son = son;
                mm_prefetch(std::addressof(m_data[son]), 0, 3);
                for (int64_t i = son + 1; i < son + 4; i++)
                {
                    if (UNLIKELY(i > (cur_size >> 2)))
                        break;
                    if (m_cmp(m_data[i], m_data[min_son]))
                        min_son = i;
                }

                if (m_cmp(m_data[u], m_data[min_son]))
                    break;

                std::swap(m_data[u], m_data[min_son]);
                u = min_son;
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
        QuaternaryHeap() = default;

        explicit QuaternaryHeap(int64_t size) { m_data.reserve(size + 1); }

        void pop()
        {
            DASSERT(!empty());
            std::swap(m_data[1], m_data.back());
            m_data.pop_back();
            if (LIKELY(!empty()))
                pushdown(1);
        }

        T pop_back()
        {
            T ans = std::move(m_data.back());
            m_data.pop_back();
            return ans;
        }

        template<typename F>
        void push(F && data)
        {
            m_data.push_back(std::forward<F>(data));
            pushup(m_data.size() - 1);
        }

        template<class ... Args>
        void emplace(Args &&... args)
        {
            m_data.emplace_back(std::forward<Args>(args)...);
            pushup(m_data.size() - 1);
        }

        template<typename F>
        void replace_top(F && data)
        {
            DASSERT(!empty());
            m_data[1] = std::forward<F>(data);
            pushdown(1);
        }

        template<typename Iterator>
        void push_all(Iterator begin, Iterator end)
        {
            if (UNLIKELY(begin == end))
                return;

            auto start_idx = m_data.size();
            m_data.insert(m_data.end(), begin, end);
            for (size_t i = start_idx; i < m_data.size(); i++)
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
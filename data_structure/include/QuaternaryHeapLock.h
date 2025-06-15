#pragma once

#include <cstddef>
#include <utility>
#include <optional>

#include "QuaternaryHeap.h"
#include "spin_lock_sleep.h"

namespace co {
    template<typename T, typename CMP = std::less<T>>
    class alignas(__CACHE_LINE__) QuaternaryHeapLock
    {
    private:
        using atomic_size_t = std::atomic<size_t>;

        QuaternaryHeap<T, CMP> m_heap{};
        size_t m_size{};
        spin_lock_sleep m_lock{};

        atomic_size_t * get_size() { return reinterpret_cast<atomic_size_t*>(&m_size); }
    public:
        QuaternaryHeapLock() = default;

        std::optional<T> try_pop()
        {
            std::lock_guard lock(m_lock);
            if (m_heap.empty())
                return std::nullopt;

            std::optional<T> ans{m_heap.top()};
            m_heap.pop();
            m_size--;
            return ans;
        }

        T wait_and_pop()
        {
            while (true)
            {
                if (empty())
                    continue;

                std::lock_guard lock(m_lock);
                if (m_heap.empty())
                    continue;

                T ans = m_heap.top();
                m_heap.pop();
                m_size--;
                return ans;
            }
        }

        template<typename F>
        void push(F && data)
        {
            std::lock_guard lock(m_lock);
            m_heap.push(std::forward<F>(data));
            m_size++;
        }

        template<class ... Args>
        void emplace(Args &&... args)
        {
            std::lock_guard lock(m_lock);
            m_heap.emplace(std::forward<Args>(args)...);
            m_size++;
        }

        [[nodiscard]] int64_t size() { return (int64_t) get_size()->load(std::memory_order_relaxed); }

        [[nodiscard]] bool empty() { return size() == 0; }
    };
}
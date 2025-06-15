#pragma once

#include <atomic>

#include "../../utils/include/spin_lock_sleep.h"

namespace co {
    template<typename T, typename Container>
    class alignas(__CACHE_LINE__) LockedContainer
    {
    private:
        Container m_data{};
        size_t m_size{};
        spin_lock_sleep m_lock{};
    public:
        LockedContainer() = default;

        template<class ... Args>
        explicit LockedContainer(Args &&... args)
        {
            m_data = Container{std::forward<Args>(args)...};
        }

        std::optional<T> try_pop()
        {
            std::lock_guard lock(m_lock);
            if (m_data.empty())
                return std::nullopt;

            T ans{};
            m_data.pop(ans);
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
                if (m_data.empty())
                    continue;

                T ans;
                m_data.pop(ans);
                m_size--;
                return ans;
            }
        }

        template<typename F>
        void push(F && data)
        {
            std::lock_guard lock(m_lock);
            m_data.push(std::forward<F>(data));
            m_size++;
        }

        [[nodiscard]] int64_t size() { return (int64_t) atomization(m_size)->load(std::memory_order_relaxed); }

        [[nodiscard]] bool empty() { return size() == 0; }
    };
}
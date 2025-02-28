#pragma once

#include <queue>
#include <memory_resource>
#include <optional>

#include "../../utils/include/spin_lock.h"
#include "../../utils/include/utils.h"

namespace co {
    template<typename T>
    class QueueLock {
    public:
        std::pmr::unsynchronized_pool_resource pool{get_default_pmr_opt()};
        std::pmr::deque<T> m_q{&pool};
        spin_lock m_lock{};

        T wait_and_pop() {
            while (true) {
                if (m_q.empty())
                    continue;

                std::lock_guard lock(m_lock);
                /* double check */
                if (m_q.empty())
                    continue;

                T ans = m_q.front();
                m_q.pop_front();
                return ans;
            }
        }

        std::optional<T> try_pop() {
            std::lock_guard lock(m_lock);
            if (m_q.empty())
                return std::nullopt;

            std::optional<T> ans{m_q.front()};
            m_q.pop_front();
            return ans;
        }

        template<typename V>
        void push(V data) {
            std::lock_guard lock(m_lock);
            m_q.push_back(std::forward<V>(data));
        }

        [[nodiscard]] int size() const { return m_q.size(); }

        [[nodiscard]] bool empty() const { return m_q.empty(); }
    };
}
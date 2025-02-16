#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include <optional>

#include "../../utils/include/spin_lock.h"
#include "QuaternaryHeap.h"

template<typename T, typename CMP = std::less<T>>
class QuaternaryHeapLock
{
private:
    QuaternaryHeap<T, CMP> m_heap{};
    std::atomic<size_t> m_size{};
    spin_lock m_lock{};
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
    void push(F data)
    {
        std::lock_guard lock(m_lock);
        m_heap.push(std::forward<F>(data));
        m_size++;
    }

    [[nodiscard]] int size() const { return m_size.load(std::memory_order_relaxed); }
    [[nodiscard]] bool empty() { return size() == 0; }
};
//
// Created by hzj on 25-1-11.
//

#pragma once

#include <utility>
#include <cmath>
#include <array>
#include <memory>

/*
template<typename T, std::size_t N>
class RingBufferSafe
{
private:
    std::atomic<std::size_t> m_size{};
    std::size_t rear{}, front{};
    std::array<T, N> m_data;

    static_assert(N > 0);
  public:
    RingBufferSafe() = default;

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

    bool push(const T & x)
    {
        if (full())
          return false;

        m_data[rear++] = x;
        rear %= N;
        m_size++;
        return true;
    }

    bool push(T && x)
    {
        if (full())
          return false;

        m_data[rear++] = std::move(x);
        rear %= N;
        m_size++;
        return true;
    }

    T && pop()
    {
        assert(!empty());
        auto tmp = front++;
        front %= N;
        return std::move(m_data[tmp]);
    }
};
 */

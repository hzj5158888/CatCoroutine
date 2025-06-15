#pragma once

#include <vector>

#include "../../utils/include/xor_shift_rand.h"

namespace co {
    template<typename T, typename RandomDevice = xor_shift_rand_64>
    class RandomStack
    {
    private:
        std::vector<T> m_data{};
        RandomDevice m_rd{};

        bool should_we_swap()
        {
            constexpr unsigned possibility = 4;
            static_assert(is_pow_of_2(possibility));
            return (m_rd() & (possibility - 1)) == 0;
        }
    public:
        RandomStack() = default;

        explicit RandomStack(const RandomDevice & rd) { this->m_rd = rd; }

        explicit RandomStack(const uint64_t seed) { this->m_rd = RandomDevice(seed); }

        template<class F>
        void push(F && x)
        {
            m_data.push_back(std::forward<F>(x));
        }

        void push_all(const std::vector<T> & vec)
        {
            m_data.insert(m_data.end(), vec.begin(), vec.end());
        }

        bool pop(T & x)
        {
            if (empty())
                return false;

            x = std::move(m_data.back());
            m_data.pop_back();
            if (m_data.size() > 1 && should_we_swap())
            {
                size_t idx = m_rd() % m_data.size();
                std::swap(m_data[idx], m_data.back());
            }

            return true;
        }

        bool pop()
        {
            if (empty())
                return false;

            m_data.pop_back();
            return true;
        }

        bool front(T & x)
        {
            if (empty())
                return false;

            x = m_data.front();
            return true;
        }

        bool back(T & x)
        {
            if (empty())
                return false;

            x = m_data.back();
            return true;
        }

        [[nodiscard]] size_t size() const { return m_data.size(); }

        [[nodiscard]] bool empty() const { return m_data.empty(); }
    };
}
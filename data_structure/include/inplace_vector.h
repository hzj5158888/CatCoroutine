#pragma once

#include <cstdint>
#include <array>

template<typename T, std::size_t N>
class inplace_vector
{
private:
    std::array<T, N> m_data;
    std::size_t m_size{};
public:
    inplace_vector() = default;
    ~inplace_vector() = default;

    template<typename V>
    void push_back(V v)
    {
        m_data.at(m_size++) = std::forward<V>(v);
    }

    T && pop_back() { return std::move(m_data.at(--m_size)); }

    T & back() { return m_data.at(m_size - 1); }

    T & front() { return m_data.at(0); }

    [[nodiscard]] int32_t size() const { return m_size; }

    void clear()
    {
        for (int i = 0; i < m_size; i++)
        {
            T t{};
            std::swap(t, m_data[i]);
        }
        
        m_size = 0;
    }

    T & at(int32_t idx) { return m_data.at(idx); }
    T & operator [] (int32_t idx) { return m_data[idx]; }
};
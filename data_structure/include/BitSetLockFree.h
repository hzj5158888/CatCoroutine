#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <mutex>

#include "../utils/include/spin_lock.h"

template<size_t BITS_COUNT = 0>
class BitSetLockFree
{
private:
    constexpr static size_t block_bits = sizeof(size_t) * 8;
    constexpr static size_t total_block = (BITS_COUNT + block_bits - 1) / block_bits;
    /* block bits is power of 2 */
    static_assert((block_bits & (block_bits - 1)) == 0);

    struct data_pos
    {
        size_t data_idx;
        uint16_t data_offset;
    };

    alignas(__CACHE_LINE__) std::array<size_t, total_block> m_block;
    alignas(__CACHE_LINE__) std::array<spin_lock, total_block> m_lock;

    data_pos get_data_pos(size_t idx) { return {idx / block_bits, static_cast<uint16_t>(idx % block_bits)}; }

    void set_bit(data_pos pos, bool val)
    {
        size_t mask;
        size_t bit_pos = block_bits - pos.data_offset - 1;
        if (!val)
        {
            mask = static_cast<size_t>(-1);
            mask -= static_cast<size_t>(1) << bit_pos;
            m_block[pos.data_idx] &= mask;
        } else {
            mask = static_cast<size_t>(1) << bit_pos;
            m_block[pos.data_idx] |= mask;
        }
    }

    bool get_bit(data_pos pos)
    {
        size_t bit_pos = block_bits - pos.data_offset - 1;
        return (m_block[pos.data_idx] >> bit_pos) & 1;
    }
public:
    constexpr static int32_t INVAILD_INDEX = -1;

    BitSetLockFree() = default;
    ~BitSetLockFree() = default;

    constexpr size_t size() { return BITS_COUNT; }

    template<typename T>
    void set(size_t idx, T val) = delete; // 禁止val隐式转换

    void set(size_t idx, bool value)
    {
        auto pos = get_data_pos(idx);
        std::lock_guard lock(m_lock[pos.data_idx]);
        set_bit(pos, value);
    }

    bool get(size_t idx)
    {
        auto pos = get_data_pos(idx);
        std::lock_guard lock(m_lock[pos.data_idx]);
        return get_bit(pos);
    }

    bool compare_exchange(size_t idx, bool expected, bool value)
    {
        auto pos = get_data_pos(idx);
        std::lock_guard lock(m_lock[pos.data_idx]);
        if (get_bit(pos) != expected)
            return false;

        set_bit(pos, value);
        return true;
    }

    int32_t change_first_expect(bool expected, bool value)
    {
        for (size_t i = 0; i < total_block; i++)
        {
            std::lock_guard lock(m_lock[i]);
            /* travel block data */
            for (uint16_t j = 0; j < block_bits; j++)
            {
                bool bit = get_bit({i, j});
                if (bit == expected)
                {
                    set_bit({i, j}, value);
                    return i * block_bits + j;
                }
            }
        }

        return INVAILD_INDEX;
    }

    /* 执行成功时，调用回调函数callback */
    template<typename Func>
    int32_t change_first_expect_then(bool expected, bool value, Func && callback)
    {
        for (size_t i = 0; i < total_block; i++)
        {
            std::lock_guard lock(m_lock[i]);
            /* travel block data */
            for (uint16_t j = 0; j < block_bits; j++)
            {
                bool bit = get_bit({i, j});
                if (bit == expected)
                {
                    set_bit({i, j}, value);
                    callback(i * block_bits + j);
                    return i * block_bits + j;
                }
            }
        }

        return INVAILD_INDEX;
    }
};

template<>
class BitSetLockFree<0>
{
public:
    constexpr static int32_t INVAILD_INDEX = -1;
};
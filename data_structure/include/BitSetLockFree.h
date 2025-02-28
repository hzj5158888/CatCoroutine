#pragma once

#include <atomic>
#include <barrier>
#include <cassert>
#include <cstddef>
#include <array>
#include <cstdint>
#include <cmath>
#include <type_traits>

#include "utils.h"

namespace co {
    template<size_t BITS_COUNT = 0>
    class BitSetLockFree {
    private:
        using block_t = size_t;
        using atomic_block = std::atomic<block_t>;

        constexpr static size_t block_bytes = sizeof(block_t);
        constexpr static size_t block_bits = block_bytes * 8;
        constexpr static size_t total_block = (BITS_COUNT + block_bits - 1) / block_bits;
        /* block bits is power of 2 */
        static_assert((block_bits & (block_bits - 1)) == 0);

        struct data_pos {
            size_t block_idx;
            uint16_t data_offset;
        };

        std::array<atomic_block, total_block> m_block{};

        template<typename Atomic, typename Fn>
        block_t atomic_fetch_modify(Atomic &atomic, Fn &&fn, std::memory_order order) {
            static_assert(std::is_invocable_r_v<block_t, Fn, block_t>);

            block_t cur = atomic.load(std::memory_order_acquire);
            block_t const &cref = cur;
            while (true) {
                cur = atomic.load(std::memory_order_relaxed);
                if (LIKELY(atomic.compare_exchange_weak(cur, fn(cref), order)))
                    break;
            }

            return cur;
        }

        data_pos get_data_pos(size_t idx) {
            size_t block_idx = idx / block_bits;
            auto byte_offset = static_cast<uint16_t>(idx % block_bits);
            return {block_idx, byte_offset};
        }

        /* 返回 pos位置 bit数据 是否被修改 */
        bool set_bit(data_pos pos, bool val, std::memory_order order) {
            size_t bit_pos = block_bits - pos.data_offset - 1;
            size_t mask = static_cast<block_t>(1) << bit_pos;
            if (!val)
                return m_block[pos.block_idx].fetch_and(~mask, order) & mask;

            return m_block[pos.block_idx].fetch_or(mask, order) & mask;
        }

        bool get_bit(data_pos pos, std::memory_order order) {
            size_t bit_pos = block_bits - pos.data_offset - 1;
            return (m_block[pos.block_idx].load(order) >> bit_pos) & 1;
        }

        long long change_first_expect_impl(bool expected, bool value) {
            long long ans = INVALID_INDEX;
            for (size_t i = 0; i < total_block; i++) {
                int bit_idx = block_bits;
                auto modify_fn = [&bit_idx, expected, value](block_t cur) -> block_t {
                    if (expected)
                        bit_idx = countl_zero(cur);
                    else
                        bit_idx = countl_one(cur);

                    //std::cout << cur << " " << bit_idx << " " << block_bits << std::endl;
                    if (bit_idx >= static_cast<int>(block_bits))
                        return cur;

                    block_t mask = static_cast<block_t>(1) << (block_bits - bit_idx - 1);
                    if (value)
                        return cur | mask;

                    return cur & ~mask;
                };

                atomic_fetch_modify(m_block[i], modify_fn, std::memory_order_seq_cst);
                /* skip this block */
                if (bit_idx >= static_cast<int>(block_bits))
                    continue;

                ans = i * block_bits + bit_idx;
                break;
            }

            return ans >= (long long) size() ? INVALID_INDEX : ans;
        }

    public:
        constexpr static long long INVALID_INDEX = -1;

        BitSetLockFree() = default;

        ~BitSetLockFree() = default;

        constexpr size_t size() { return BITS_COUNT; }

        // 禁止value隐式转换
        template<typename T>
        void set(size_t idx, T value, std::memory_order order = std::memory_order_seq_cst) = delete;

        void set(size_t idx, bool value, std::memory_order order = std::memory_order_seq_cst) {
            auto pos = get_data_pos(idx);
            set_bit(pos, value, order);
        }

        bool get(size_t idx, std::memory_order order = std::memory_order_seq_cst) {
            auto pos = get_data_pos(idx);
            return get_bit(pos, order);
        }

        bool compare_set(size_t idx, bool expected, bool value, std::memory_order order = std::memory_order_seq_cst) {
            if (idx >= size())
                return false;

            auto pos = get_data_pos(idx);
            bool ans = true;
            auto fn = [pos, expected, value, &ans](block_t cur) -> block_t {
                size_t bit_pos = block_bits - pos.data_offset - 1;
                size_t mask = static_cast<block_t>(1) << bit_pos;

                bool cur_bit = (cur & mask) >> bit_pos;
                if (cur_bit != expected) {
                    ans = false;
                    return cur;
                }

                if (!value)
                    return cur & ~mask;
                else
                    return cur | mask;
            };

            atomic_fetch_modify(m_block[pos.block_idx], fn, order);
            return ans;
        }

        void flip() {
            for (size_t i = 0; i < total_block; i++) {
                atomic_fetch_modify(
                        m_block[i],
                        [](block_t val) { return ~val; },
                        std::memory_order_seq_cst
                );
            }
        }

        long long change_first_expect(bool expected, bool value) {
            return change_first_expect_impl(expected, value);
        }
    };

    template<>
    class BitSetLockFree<0> {
    public:
        constexpr static long long INVALID_INDEX = -1;
    };

}
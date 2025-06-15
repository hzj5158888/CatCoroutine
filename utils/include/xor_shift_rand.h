#pragma once

#include "utils.h"
#include <cstdint>

namespace co {
    inline uint32_t xor_shift_32(uint32_t state) {
        /* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /* The state must be initialized to non-zero */
    inline uint64_t xor_shift_64(uint64_t state)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    class xor_shift_32_rand
    {
    public:
        uint32_t cur_rand{};

        xor_shift_32_rand() : cur_rand(1) {}
        explicit xor_shift_32_rand(uint64_t x) : cur_rand(x) {}

        uint32_t operator() ()
        {
            cur_rand = xor_shift_32(cur_rand);
            return cur_rand;
        }

        void swap(xor_shift_32_rand && oth)
        {
            std::swap(cur_rand, oth.cur_rand);
        }
    };

    class xor_shift_rand_64
    {
    public:
        uint64_t cur_rand{};

        xor_shift_rand_64() : cur_rand(1) {}
        explicit xor_shift_rand_64(uint64_t x) : cur_rand(x) {}

        uint32_t operator() ()
        {
            cur_rand = xor_shift_64(cur_rand);
            return cur_rand;
        }

        void swap(xor_shift_rand_64 && oth)
        {
            std::swap(cur_rand, oth.cur_rand);
        }
    };
}
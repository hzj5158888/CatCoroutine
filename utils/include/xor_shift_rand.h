#pragma once

#include "utils.h"
#include <cstdint>

class xor_shift_32_rand
{
public:
    uint32_t cur_rand{};

    xor_shift_32_rand() : cur_rand(1) {}
    xor_shift_32_rand(uint32_t x) : cur_rand(x) {}

    uint32_t operator () ()
    {
        cur_rand = xorshift32(cur_rand);
        return cur_rand;
    }
};
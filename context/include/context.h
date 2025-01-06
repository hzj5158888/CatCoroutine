#pragma once

#include <cstdint>

constexpr std::size_t MAX_STACK_SIZE = 1024 * 1024 * 2; // 2MB
constexpr std::size_t MIN_STACK_SIZE = 128; // 128B

struct stack_context
{
    uint8_t * stk;
    void * sp, * bp;

    std::size_t size() const { return (std::size_t)sp - (std::size_t)bp; }
};

struct context
{
    stack_context stk;
};

context make_context(); // 创建新context，并初始化
void save_context(context *); // 保存当前context
void switch_context(const context &); // 切换新context
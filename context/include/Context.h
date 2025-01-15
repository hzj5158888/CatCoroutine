#pragma once

#include <cstdint>
#include <bits/wordsize.h>
#include <csetjmp>
#include <cstddef>

#include "../allocator/include/MemoryPool.h"
#include "../allocator/include/StackPoolDef.h"

constexpr std::size_t SAVE_SIG_MASK = 1;
constexpr std::size_t SIGSET_WORDS = (1024 / (8 * sizeof (unsigned long int)));

enum
{
    CONTEXT_CONTINUE = 0,
    CONTEXT_RESTORE
};

struct Context
{
    uint8_t * stk_dyn{};
	void * stk_dyn_mem{};
	uint32_t stk_dyn_capacity{};
	bool stk_is_static{false};
	StackPool * static_stk_pool{};
	MemoryPool * stk_dyn_alloc{};

    __attribute__((aligned(64)))
	struct {
#if defined  __x86_64__
        uint64_t bx;
        uint64_t bp;
        uint64_t r12, r13, r14, r15;
        uint64_t sp;
        uint64_t ip; // rip, program counter
#else
        void * bx;
        void * si, * di; // index register
        void * bp, * sp; // stack
        void * ip; // eip
#endif
    } jmp_reg{};

    int mask_was_saved{false};
    unsigned long int sig_mask[SIGSET_WORDS]{};

#if defined __x86_64__
    struct {
        uint64_t di, si; // 2 arg is enough
    } arg_reg{};

    uint8_t first_make{true};
#endif

    [[nodiscard]] std::size_t stk_size() const { return (std::size_t)jmp_reg.sp - (std::size_t)jmp_reg.bp; }

	void set_stack(const uint8_t * stk)
	{
		std::size_t size = stk_size();
		jmp_reg.bp = reinterpret_cast<uint64_t>(stk);
		jmp_reg.sp = jmp_reg.bp + size;
	}

    struct __jmp_buf_tag * get_jmp_buf() { return reinterpret_cast<struct __jmp_buf_tag*>(&jmp_reg); }
};

int save_context(Context *); // 保存当前context

void switch_context(Context *); // 切换新context

void make_context
(
        Context * ctx,
        void * stk,
        void (*wrap)(void (*)(void*), void *),
        void (*func)(void *),
        void * arg
);

#if defined __x86_64__
extern "C" void switch_context_first_make(__jmp_buf_tag *, uint64_t rdi, uint64_t rsi);
#endif
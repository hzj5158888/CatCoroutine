#pragma once

#include <cstdint>
#include <bits/wordsize.h>
#include <csetjmp>
#include <cstddef>
#include <iostream>
#include <cassert>
#include <new>

#include "../allocator/include/MemoryPool.h"
#include "../allocator/include/StackPoolDef.h"
#include "../allocator/include/DynStackPoolDef.h"

constexpr std::size_t SAVE_SIG_MASK = 1;
constexpr std::size_t SIGSET_WORDS = (1024 / (8 * sizeof (unsigned long int)));

enum
{
    CONTEXT_CONTINUE = 0,
    CONTEXT_RESTORE,
	CALL_YIELD,
	CALL_AWAIT,
	CALL_DEAD
};

struct Context
{
	bool stk_is_static{false};
	StackPool * static_stk_pool{};
	int32_t occupy_stack{-1};
	uint8_t * stk_real_bottom{}; // real stack bottom
	std::size_t stk_size{};

    uint8_t * stk_dyn{};
	void * stk_dyn_mem{};
	std::size_t stk_dyn_size{};
	uint32_t stk_dyn_capacity{};
	DynStackPool * stk_dyn_alloc{};
	MemoryPool * stk_dyn_saver_alloc{};
	uint8_t * stk_dyn_real_bottom{};

	/* System V Abi calling convention */
	/* preserved across function calls */
	alignas(__CACHE_LINE__) struct {
#if defined  __x86_64__
        uint64_t bx;
        uint64_t bp;
        uint64_t r12, r13, r14, r15;
        uint64_t sp;
        uint64_t ip; // rip, program counter
		uint32_t mxcsr; // sse2 control and status word, offset=64
		uint16_t x87cw; // x87 fpu control word, offset=68
#else
        void * bx;
        void * si, * di; // index register
        void * bp, * sp; // stack
        void * ip; // eip
#endif
    } jmp_reg{};

/* pass arguments by switch_context_first_run */
#if defined __x86_64__
    struct {
        uint64_t di, si; // 2 arg is enough
    } arg_reg{};

    bool first_full_save{true};
#endif

	[[nodiscard]] std::size_t stk_frame_size() const { return (std::size_t)jmp_reg.bp - (std::size_t)jmp_reg.sp; }
    /* 更新 stk size，save_context后使用 */
	void set_stk_size() { stk_size = (std::size_t)stk_real_bottom - jmp_reg.sp; }
	/* bottom地址更新前使用, 仅当 独享模式 使用 */
	void set_stk_dyn_size() { stk_dyn_size = (std::size_t)stk_dyn_real_bottom - jmp_reg.sp; }

	void set_stack(uint8_t * stk)
	{
		std::size_t frame_size = stk_frame_size();

		if (stk_real_bottom == nullptr) // first set
		{
			jmp_reg.bp = reinterpret_cast<uint64_t>(stk);
			jmp_reg.sp = jmp_reg.bp - stk_size;
		} else {
			jmp_reg.bp = jmp_reg.bp - (std::size_t)stk_real_bottom + (std::size_t)stk;
			jmp_reg.sp = jmp_reg.bp - frame_size;
		}

		stk_real_bottom = stk;
	}

	void set_stack_dyn(uint8_t * stk)
	{
		std::size_t frame_size = stk_frame_size();

		if (stk_dyn_real_bottom == nullptr) // first set
		{
			jmp_reg.bp = reinterpret_cast<uint64_t>(stk);
			jmp_reg.sp = jmp_reg.bp - stk_dyn_size;
		} else {
			jmp_reg.bp = jmp_reg.bp - (std::size_t)stk_dyn_real_bottom + (std::size_t)stk;
			jmp_reg.sp = jmp_reg.bp - frame_size;
		}

		stk_dyn_real_bottom = stk;
	}

	void prefetch_stk() const { __builtin_prefetch((void*)jmp_reg.bp, 1, 3); }

	void assert_stack() { assert(jmp_reg.bp > 0 && jmp_reg.sp > 0); }

    void * get_jmp_buf() { return std::launder(&jmp_reg); }
};

void make_context_wrap
(
        Context * ctx,
        void (*wrap)(void (*)(void*), void *),
        void (*func)(void *),
        void * arg
);

void make_context
(
	Context * ctx,
	void (*func)(void *),
	void * arg
);

void switch_context(Context *, int ret = CONTEXT_RESTORE); // 切换新context
#if defined __x86_64__
extern "C" int save_context(void * jmp_buf, bool * first_full_save); // 汇编实现
extern "C" int _switch_context(void * jmp_buf, int ret); // 汇编实现
extern "C" void switch_context_first_full_save(void *, uint64_t rdi, uint64_t rsi); // 汇编实现
#endif
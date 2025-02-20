#pragma once

#include <cstdint>
#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <cassert>
#include <new>
#include <memory_resource>

#include "../data_structure/include/ListLockFree.h"

#include "../allocator/include/MemoryPool.h"
#include "../allocator/include/StackPoolDef.h"
#include "../allocator/include/DynStackPoolDef.h"
#include "Coroutine.h"

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
#ifdef __MEM_PMR__
	std::pmr::synchronized_pool_resource * stk_dyn_saver_alloc{};
#else
	MemoryPool * stk_dyn_saver_alloc{};
#endif
	uint8_t * stk_dyn_real_bottom{};

	/* System V Abi calling convention */
	/* preserved across function calls */
	struct JMP_REG
    {
#if defined  __x86_64__
        constexpr static auto REG_COUNT = 12;
        constexpr static auto X87CW = 0;
        constexpr static auto SI = 7;
        constexpr static auto DI = 8;
        constexpr static auto BP = 10;
        constexpr static auto IP = 11;
        uint64_t bp, sp;
#else
        void * bx;
        void * si, * di; // index register
        void * bp, * sp; // stack
        void * ip; 		 // eip
#endif
    } jmp_reg{};

    /* pass 2 arguments by switch_context_first_run */
#if defined __x86_64__
    struct {
        uint64_t di, si; // 2 arg is enough
    } arg_reg{};

    bool first_full_save{true};
#endif

    [[nodiscard]] uint64_t * get_stk() const { return reinterpret_cast<uint64_t*>(jmp_reg.bp); }
	[[nodiscard]] std::size_t stk_frame_size() const { return (std::size_t)get_stk()[JMP_REG::BP] - (std::size_t)jmp_reg.sp; }
    /* 更新 stk size，save_context后使用 */
	void set_stk_size() { stk_size = (std::size_t)stk_real_bottom - jmp_reg.sp; }
	/* bottom地址更新前使用, 仅当 独享模式 使用 */
	void set_stk_dyn_size() { stk_dyn_size = (std::size_t)stk_dyn_real_bottom - jmp_reg.sp; }

	void set_stack(uint8_t * stk)
	{
		if (stk_real_bottom == nullptr) // first set
		{
			jmp_reg.bp = reinterpret_cast<uint64_t>(stk);
			jmp_reg.sp = jmp_reg.bp;
		} else {
            auto cur_stk = get_stk();
            std::size_t frame_size = stk_frame_size();
            cur_stk[JMP_REG::BP] = cur_stk[JMP_REG::BP] - (std::size_t)stk_real_bottom + (std::size_t)stk;
			jmp_reg.sp = cur_stk[JMP_REG::BP] - frame_size;
		}

		stk_real_bottom = stk;
	}

	void set_stack_dyn(uint8_t * stk)
	{
		if (stk_dyn_real_bottom == nullptr) // first set
		{
			jmp_reg.bp = reinterpret_cast<uint64_t>(stk);
			jmp_reg.sp = jmp_reg.bp;
		} else {
            auto cur_stk = get_stk();
            std::size_t frame_size = stk_frame_size();
            cur_stk[JMP_REG::BP] = cur_stk[JMP_REG::BP] - (std::size_t)stk_dyn_real_bottom + (std::size_t)stk;
			jmp_reg.sp = cur_stk[JMP_REG::BP] - frame_size;
		}

		stk_dyn_real_bottom = stk;
	}

	void assert_stack() const
	{
		DASSERT(jmp_reg.bp > 0 && jmp_reg.sp > 0 && jmp_reg.ip > 0);
		if (stk_is_static)
		{
			DASSERT((size_t)stk_real_bottom - jmp_reg.sp <= co::MAX_STACK_SIZE);
			DASSERT((size_t)stk_real_bottom >= jmp_reg.bp);
		} else {
            if (stk_dyn_real_bottom == nullptr)
                return;

            DASSERT((size_t)stk_dyn_real_bottom - jmp_reg.sp <= co::MAX_STACK_SIZE);
            DASSERT((size_t)stk_dyn_real_bottom >= jmp_reg.bp);
        }
	}

    void * get_jmp_buf() { return std::launder(std::addressof(jmp_reg)); }
};

extern "C" int swap_context_impl(uint64_t * from, uint64_t * to, int ret = CONTEXT_RESTORE);
int swap_context(Context * from, Context * to, int ret = CONTEXT_RESTORE);

void make_context_wrap
(
        Context * ctx,
        void (*wrap)(void (*)(void*), void *)
);
void make_context
(
	    Context * ctx,
	    void (*func)(void *)
);

void switch_context(Context *, int ret = CONTEXT_RESTORE); // 切换新context
#if defined __x86_64__
extern "C" int save_context(
        void * jmp_buf,
        bool * first_full_save
); // 汇编实现
extern "C" int _switch_context(
        void * jmp_buf,
        int ret
);
extern "C" void switch_context_first_full_save(void *, uint64_t rdi, uint64_t rsi); // 汇编实现
#endif
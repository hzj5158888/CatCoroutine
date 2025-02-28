//
// Created by hzj on 25-1-14.
//

#pragma once

#include <cstdint>

#ifdef __MEM_PMR__
#include <memory_resource>
#endif

#include "MemoryPool.h"
#include "StackPoolDef.h"
#include "../../include/Coroutine.h"
#include "../../utils/include/spin_lock.h"
#include "../../include/CoDef.h"
#include "../data_structure/include/BitSetLockFree.h"
#include "utils.h"

namespace co {
    struct StackInfo {
        constexpr static std::size_t STACK_ALIGN = 16;
        constexpr static std::size_t STACK_RESERVE = 4 * STACK_ALIGN;
        constexpr static std::size_t STACK_SIZE = co::STATIC_STACK_SIZE + STACK_RESERVE;

        enum STACK_STATUS {
            FREED = 1,
            RELEASED,
            ACTIVE
        };

        uint8_t *stk{};
        Co_t *occupy_co{};
        uint8_t stk_status{FREED};
        spin_lock m_write_back_lock{};

        StackInfo() { stk = static_cast<uint8_t *>(std::malloc(STACK_SIZE)); }

        ~StackInfo() { std::free(stk); }

        void wait_write_back() { m_write_back_lock.wait_until_lockable(); }

        void lock_write_back() { m_write_back_lock.lock(); }

        void unlock_write_back() { m_write_back_lock.unlock(); }

        [[nodiscard]] uint8_t *get_stk_bp_ptr() const {
            auto *ptr = &stk[co::MAX_STACK_SIZE];
            return align_stk_ptr(ptr);
        }
    };

    struct StackPool {
        std::array<StackInfo, 1> stk{};

#ifdef __MEM_PMR__
        std::pmr::synchronized_pool_resource dyn_stk_saver_pool{get_default_pmr_opt()};
#else
        MemoryPool dyn_stk_saver_pool{co::MAX_STACK_SIZE * 2, false};
#endif

        spin_lock m_lock{};
        BitSetLockFree<co::STATIC_STK_NUM> freed_stack{};
        BitSetLockFree<co::STATIC_STK_NUM> released_co{};
        BitSetLockFree<co::STATIC_STK_NUM> running{};

        StackPool();

        void alloc_dyn_stk_mem(void *&mem_ptr, std::size_t size);

        void write_back(StackInfo *info);

        void setup_co_static_stk(Co_t *co, uint8_t *stk_ptr);

        void destroy_stack(Co_t *co);

        void release_stack(Co_t *co);

        void alloc_static_stk(Co_t *co);

        void unique_assert(int);
    };
}
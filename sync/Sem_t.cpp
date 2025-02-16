//
// Created by hzj on 25-1-14.
//

#include <mutex>
#include <memory_resource>
#include <algorithm>

#include "../include/CoPrivate.h"
#include "../include/CoCtx.h"
#include "include/Sem_t.h"
#include "../sched/include/Scheduler.h"
#include "../utils/include/atomic_utils.h"

void Sem_t::wait()
{
    int64_t token{};
	if ((token = (--count)) >= 0)
		return;

    /*
    spinning_count++;
    auto wait_fn = [this, token]() -> bool
    {
        return token < count.load(std::memory_order_relaxed);
    };
    if (spin_wait(cur_max_spin, wait_fn))
    {
        atomic_fetch_modify(cur_max_spin, [](int m_spin) -> int
        {
            if (m_spin * 2 > MAX_SPIN / 2)
                return std::min(m_spin + SPIN_LEVEL, MAX_SPIN);

            return m_spin << 1;
        });

        return;
    }
    if (token < count.load(std::memory_order_acquire))
        return;

    spinning_count--;
    atomic_fetch_modify(cur_max_spin, [](int m_spin) { return std::max(m_spin >> 2, MIN_SPIN); });
*/

    /* save context before apply to wait queue */
    auto scheduler = co_ctx::loc->scheduler.get();
    auto co = scheduler->running_co;
    DASSERT(co != nullptr);
    DASSERT((uint64_t)co->ctx.get_jmp_buf() > 1024 * 1024 * 1024);
    int res = save_context(co->ctx.get_jmp_buf(), &co->ctx.first_full_save);
    if (res == CONTEXT_RESTORE)
        return;

    /* update stack size */
#ifdef __STACK_STATIC__
    co->ctx.set_stk_size();
    co->ctx.static_stk_pool->release_stack(co);
#elif __STACK_DYN__
    co->ctx.set_stk_dyn_size();
#endif

    scheduler->interrupt(CO_WAITING);
    scheduler->remove_from_scheduler(co);
#ifdef __DEBUG__
    co->sem_ptr = this;
#endif
	wait_q[co->sched.occupy_thread].push(co);
	// only running coroutine can use Sem
	// doesn't need to remove from scheduler
    scheduler->jump_to_sched();
}

bool Sem_t::try_wait()
{
    auto sub_count_fn = [](int64_t cur_count) -> int64_t
    {
        if (cur_count <= 0)
            return cur_count;

        return cur_count - 1;
    };
    auto cnt = atomic_fetch_modify(count, sub_count_fn);
	return cnt > 0;
}

std::optional<Co_t*> Sem_t::try_pick_from_wait_q()
{
    for (int i = 0; i < (int)co::CPU_CORE; i++)
    {
        if (wait_q[i].empty())
            continue;

        auto co_opt = wait_q[i].try_pop();
        if (co_opt.has_value())
            return co_opt;
    }

    return std::nullopt;
}

void Sem_t::signal()
{
	if ((++count) > 0)
		return;

    /*
    if (cur_signal_spin_cycle.load(std::memory_order_relaxed) > HANDLE_QUEUE_CYCLE)
    {
        auto co_opt = try_pick_from_wait_q();
        if (co_opt.has_value())
        {
            cur_signal_spin_cycle.store(0, std::memory_order_release);
            co_ctx::manager->apply(co_opt.value());
            return;
        }
    }
    auto spinning_sub_fn = [](int32_t cur_spin_count) -> int32_t
    {
       if (cur_spin_count > 0)
           return cur_spin_count - 1;

        return cur_spin_count;
    };
    if (atomic_fetch_modify(spinning_count, spinning_sub_fn) > 0)
    {
        cur_signal_spin_cycle.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
     */

	// push_back to scheduler
    cur_signal_spin_cycle.store(0, std::memory_order_release);
    Co_t * co{};
    while (co == nullptr)
    {
        for (int i = 0; i < (int)co::CPU_CORE; i++)
        {
            if (wait_q[i].empty())
                continue;

            auto co_opt = wait_q[i].try_pop();
            if (co_opt.has_value())
            {
                co = co_opt.value();
                break;
            }
        }
    }
#ifdef __DEBUG__
    co->sem_ptr = nullptr;
#endif
    co_ctx::manager->apply(co);
}

void Sem_t::release(Sem_t * sem)
{
    int sum{};
    for (auto & q : sem->wait_q)
        sum += q.size();

    if (UNLIKELY(sem->count != sem->init_count || sum != 0))
    {
        throw SemClosedException();
    }

	sem->~Sem_t();
	if (LIKELY(sem->alloc != nullptr))
#ifdef __MEM_PMR__
		sem->alloc->deallocate(sem, sizeof(Sem_t));
#else
		sem->alloc->deallocate(sem);
#endif
	else
		std::free(sem);
}

Sem_t::operator int64_t () { return count; }

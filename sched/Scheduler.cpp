#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Scheduler.h"
#include "../utils/include/co_utils.h"
#include "../utils/include/atomic_utils.h"
#include "CoPrivate.h"
#include "Context.h"
#include "utils.h"

void Scheduler::push_to_ready(Co_t * co, bool enable_lock)
{
	std::unique_lock<spin_lock_t> lock;
#if __SCHED_RB_TREE__
	if (enable_lock)
		m_lock = std::unique_lock(sched_lock);

	ready.insert(co);
#elif __SCHED_HEAP__
	if (enable_lock)
		lock = std::unique_lock(sched_lock);

	if (co->sched.can_migration)
		ready.push(co);
	else
		ready_fixed.push(co);

#ifdef __DEBUG_SCHED__
    co_ctx::m_lock.lock();
    co_ctx::co_vec.insert(co);
    co_ctx::m_lock.unlock();
#endif

#elif __SCHED_FIFO__
    if (co->sched.can_migration)
		ready.push(co);
    else
		ready_fixed.push(co);
#endif
}

void Scheduler::push_all_to_ready(const std::vector<Co_t*> & co_vec, const std::vector<Co_t*> & fixed_co, bool enable_lock)
{
#ifdef __SCHED_HEAP__
	std::unique_lock<spin_lock_t> lock;
	if (enable_lock)
		lock = std::unique_lock(sched_lock);

	ready.push_all(co_vec);
	ready_fixed.push_all(fixed_co);
#else
	static_assert(false);
#endif
}

void Scheduler::pull_half_co(std::vector<Co_t*> & ans)
{
    if (ready.empty())
        return;

#ifdef __SCHED_CFS__
	std::lock_guard lock(sched_lock);
    /* double check */
    if (ready.empty())
        return;
#endif
    int trans_size = 0;
	int pull_count = ready.size() / 2;
	for (; trans_size < pull_count; trans_size++)
	{
        if (!sem_ready.try_wait())
            break;

#ifdef __SCHED_CFS__
#ifdef __SCHED_HEAP__
        ans.push_back(ready.top());
		ready.pop();
#else
        static_assert(false);
#endif
#elif __SCHED_FIFO__
        auto co_opt = ready.try_pop();
        if (!co_opt.has_value())
            break;

        ans.push_back(co_opt.value());
#endif
#ifdef __DEBUG_SCHED__
        co_ctx::m_lock.lock();
        co_ctx::co_vec.erase(ans.back());
        co_ctx::m_lock.unlock();
#endif
		remove_from_scheduler(ans.back());
		ans.back()->sched.occupy_thread = -1;
	}
    ready_count.fetch_sub(trans_size);
}

void Scheduler::pull_from_buffer(std::vector<Co_t*> & ans)
{
	Co_t * cur{};
	for (int i = 0; i < APPLY_BUFFER_SIZE && buffer.pop(cur); i++)
		ans.push_back(cur);
}

std::vector<Co_t*> Scheduler::pull_from_buffer()
{
	std::vector<Co_t*> ans{};
	ans.reserve(APPLY_BUFFER_SIZE);
	pull_from_buffer(ans);
	return ans;
}

void Scheduler::apply_ready(Co_t * co)
{
	uint64_t cur_sum_v_runtime{};
	get_ready_to_push(co, cur_sum_v_runtime);
	if (sched_lock.try_lock_for_backoff(MAX_BACKOFF))
	{
		push_to_ready(co, false);
		sched_lock.unlock();
		sum_v_runtime += cur_sum_v_runtime;
		ready_count.fetch_add(1);
		sem_ready.signal();
	} else {
		apply_ready_lazy(co);
	}
}

void Scheduler::apply_ready_lazy(Co_t * co)
{
	if (UNLIKELY(!buffer.push(co)))
	{
		auto buf_co = pull_from_buffer();
		buf_co.push_back(co);
		apply_ready_all(buf_co, true);
	}
	sem_ready.signal();
}

void Scheduler::get_ready_to_push(Co_t * co, uint64_t & sum_v_runtime)
{
#ifdef __SCHED_CFS__
	if (co->sched.v_runtime == 0)
		co->sched.v_runtime = min_v_runtime.load(std::memory_order_acquire);

	sum_v_runtime += co->sched.v_runtime;
#endif

#ifdef __DEBUG__
    if (!co->sched.can_migration)
        DASSERT(co->sched.occupy_thread == -1 || co->sched.occupy_thread == this_thread_id);
#endif

	co->status_lock.lock();
	co->status = CO_READY;
	co->status_lock.unlock();
	co->sched.occupy_thread = this_thread_id;
	co->scheduler = this;
}

void Scheduler::apply_ready_eager(Co_t * co, bool from_buffer)
{
	uint64_t cur_sum_v_runtime{};
	get_ready_to_push(co, cur_sum_v_runtime);
	sum_v_runtime += cur_sum_v_runtime;
	push_to_ready(co, true);
	ready_count.fetch_add(1);
	if (!from_buffer)
		sem_ready.signal();
}

void Scheduler::apply_ready_all(const std::vector<Co_t *> & co_vec, bool from_buffer)
{
	if (co_vec.empty())
		return;

#ifdef __SCHED_CFS__
	uint64_t cur_sum_v_runtime = 0;
#endif
	for (auto co : co_vec)
		get_ready_to_push(co, cur_sum_v_runtime);

#ifdef __SCHED_CFS__
	sum_v_runtime.fetch_add(cur_sum_v_runtime, std::memory_order_acq_rel);
#endif

#ifdef __SCHED_CFS__
	sched_lock.lock();
#endif
	std::vector<Co_t*> fixed_co{};
	std::vector<Co_t*> unfixed_co{};
	fixed_co.reserve(co_vec.size() / 2);
	unfixed_co.reserve(co_vec.size() / 2);
	for (auto co : co_vec)
	{
		if (co->sched.can_migration)
			unfixed_co.push_back(co);
		else
			fixed_co.push_back(co);
	}

	push_all_to_ready(unfixed_co, fixed_co, false);
#ifdef __SCHED_CFS__
    sched_lock.unlock();
#endif

	ready_count.fetch_add(co_vec.size());
	if (!from_buffer)
		sem_ready.signal(co_vec.size());
}

void Scheduler::remove_from_scheduler(Co_t * co)
{
#ifdef __SCHED_CFS__
	sum_v_runtime -= co->sched.v_runtime;
#endif
	co->scheduler = nullptr;
}

Co_t * Scheduler::pickup_ready()
{
	if (!sem_ready.try_wait())
	{
		auto res = co_ctx::manager->stealing_work(this_thread_id);
		apply_ready_all(res, false);
		sem_ready.wait();
	}
	
	/* block: pull from buffer */
	{
		std::vector<Co_t*> res{};
		while (ready_count.load(std::memory_order_relaxed) == 0)
		{
			pull_from_buffer(res);
			if (res.empty())
				continue;

			apply_ready_all(res, true);
			break;
		}
	}

	ready_count.fetch_sub(1);

#ifdef __DEBUG__
    sched_lock.lock();
    DASSERT(!(ready.empty() && ready_fixed.empty()));
    sched_lock.unlock();
#endif

#ifdef __SCHED_CFS__
#ifdef __SCHED_RB_TREE__
	sched_lock.m_lock();
	auto iter = ready.begin();
	auto ans = *iter;
	ready.erase(iter);
	if (!ready.empty()) [[likely]]
		min_v_runtime = (*ready.begin())->sched.v_runtime;

	sched_lock.unlock();
#elif __SCHED_HEAP__
	sched_lock.lock();

	Co_t * ans{};
	Co_t * ans_list[2]{nullptr, nullptr};
	if (!ready.empty())
		ans_list[0] = ready.top();
	if (!ready_fixed.empty())
		ans_list[1] = ready_fixed.top();

	DASSERT(ans_list[0] || ans_list[1]);

	if (CoPtrLessCmp{}(ans_list[0], ans_list[1]))
	{
		ans = ans_list[0];
		ready.pop();
	} else {
		ans = ans_list[1];
		ready_fixed.pop();
	}

	auto up_v_runtime_fn = [this](uint64_t cur_v_time) -> uint64_t
	{
		if (!ready.empty())
			cur_v_time = std::min(cur_v_time, ready.top()->sched.v_runtime);
		if (!ready_fixed.empty())
			cur_v_time = std::min(cur_v_time, ready_fixed.top()->sched.v_runtime);

		return cur_v_time;
	};
	atomic_fetch_modify(min_v_runtime, up_v_runtime_fn, std::memory_order_acq_rel);
	
	sched_lock.unlock();
#else
    static_assert(false);
#endif
#elif __SCHED_FIFO__
    std::optional<Co_t*> ans_opt{};
    while (!ans_opt.has_value())
    {
        cur_pick++;

        if (!(cur_pick & 1))
            ans_opt = ready.try_pop();
        if (!ans_opt.has_value())
            ans_opt = ready_fixed.try_pop();
    }

    DASSERT(ans_opt.has_value());
    auto ans = ans_opt.value();
#else
    static_assert(false);
#endif

    DASSERT(ans->status == CO_READY);
#ifdef __DEBUG_SCHED_READY__
    co_ctx::m_lock.lock();
    co_ctx::co_vec.erase(ans);
    co_ctx::m_lock.unlock();
#endif
	return ans;
}

void Scheduler::run(Co_t * co)
{
	assert(UNLIKELY(co->status == CO_READY));

	co->status_lock.lock();
    co->stk_active_lock.lock();
	co->sched.start_exec();
	if (LIKELY(co->id != co::MAIN_CO_ID)) // 设置 非main coroutine 栈帧
	{
#ifdef __STACK_STATIC__
        /* 设置stack分配器 */
		if (UNLIKELY(co->ctx.static_stk_pool == nullptr))
			co->ctx.static_stk_pool = &co_ctx::loc->alloc.stk_pool;

		/* 分配堆栈 */
		co->ctx.static_stk_pool->alloc_static_stk(co);
        if (UNLIKELY(co->ctx.first_full_save))
            make_context_wrap(&co->ctx, &co::wrap);
#elif __STACK_DYN__
        if (UNLIKELY(co->ctx.stk_dyn_alloc == nullptr))
        {
            co_ctx::loc->alloc.dyn_stk_pool.alloc_stk(&co->ctx);
            make_context_wrap(&co->ctx, &co::wrap);
        }
#endif
	}

#ifdef __DEBUG_SCHED_RUN__
    co_ctx::m_lock.lock();
    DASSERT(co_ctx::running_co.count(co) == 0);
    co_ctx::running_co.insert(co);
    co_ctx::m_lock.unlock();
#endif
	running_co = co;
	co->status = CO_RUNNING;
	co->sched.start_exec();
	co->status_lock.unlock();
    latest_arg = swap_context(&sched_ctx, &co->ctx);
}

/* Coroutine 中断执行 */
Co_t * Scheduler::interrupt(int new_status, bool unlock_exit)
{
	DASSERT(running_co != nullptr);
	running_co->status_lock.lock();
	/* update the status */
	running_co->status = new_status;
    running_co->sched.end_exec();
#ifdef __SCHED_CFS__
	/* update running time */
	sum_v_runtime.fetch_sub(running_co->sched.v_runtime, std::memory_order_acq_rel);
	running_co->sched.up_v_runtime();
	sum_v_runtime.fetch_add(running_co->sched.v_runtime, std::memory_order_acq_rel);
#endif
	/* unlock before exit */
	if (unlock_exit)
		running_co->status_lock.unlock();

    return running_co;
}

uint64_t Scheduler::get_load()
{
    uint64_t load{};
#ifdef __SCHED_CFS__
   // load += sum_v_runtime.load(std::memory_order_relaxed);
#endif
	load += ready_count.load(std::memory_order_relaxed);
	return load;
}

void Scheduler::coroutine_yield()
{
	/* interrupt coroutine */
	auto yield_co = interrupt(CO_INTERRUPT, false);
	remove_from_scheduler(yield_co);

	/* yield finish */
	yield_co->status_lock.unlock();

	/* apply to scheduler */
	co_ctx::manager->apply(yield_co);
}

void Scheduler::coroutine_await()
{
	DASSERT(await_callee != nullptr);
    DASSERT(await_callee->status != CO_DEAD);

	auto caller = interrupt(CO_WAITING, false);
    await_callee->await_caller_lock.lock();
	await_callee->await_caller.push(caller);
    await_callee->await_caller_lock.unlock();
	caller->await_callee = await_callee;
	/* release caller */
	caller->status_lock.unlock();
    /* release callee */
    await_callee->status_lock.unlock();
	await_callee = nullptr;
}

void Scheduler::coroutine_dead()
{
	auto dead_co = interrupt(CO_DEAD, false);
	DASSERT(dead_co != nullptr);

	/* wakeup waiting coroutine */
	co_ctx::manager->wakeup_await_co_all(dead_co);

	/* 释放栈空间 */
#ifdef __STACK_DYN__
	if (dead_co->ctx.stk_dyn_mem != nullptr) [[likely]]
	{
		auto stk_pool = dead_co->ctx.stk_dyn_alloc;
		stk_pool->free_stk(&dead_co->ctx);
	}
#elif __STACK_STATIC__
	if (dead_co->id != co::MAIN_CO_ID) [[likely]]
	{
		auto stk_pool = dead_co->ctx.static_stk_pool;
		stk_pool->destroy_stack(dead_co);
	}
#endif

#ifdef __DEBUG_SCHED_READY__
    co_ctx::m_lock.lock();
    co_ctx::co_vec.erase(dead_co);
    co_ctx::m_lock.unlock();
#endif

	dead_co->status = CO_DEAD;
	dead_co->status_lock.unlock();
}

void Scheduler::process_callback(int arg)
{
	switch (arg)
	{
	case CALL_AWAIT:
		coroutine_await();
		break;
	case CALL_YIELD:
		coroutine_yield();
		break;
	case CALL_DEAD:
		coroutine_dead();
		break;
	case CONTEXT_RESTORE:
		break;
	default:
		assert(false && "unknown arg");
	}
}

[[noreturn]] void Scheduler::start()
{
	while (true)
	{
        int res = latest_arg;
        if (res != CONTEXT_CONTINUE)
        {
            process_callback(res);
            latest_arg = CONTEXT_CONTINUE; // clear arg
            continue;
        }

		auto co = pickup_ready();
		run(co);
#ifdef __STACK_STATIC__
        co->ctx.set_stk_size();

        /* release static stack */
        if (LIKELY(co->ctx.static_stk_pool != nullptr))
            co->ctx.static_stk_pool->release_stack(co);
#else
        co->ctx.set_stk_dyn_size();
#endif
#ifdef __DEBUG_SCHED_RUN__
        co_ctx::m_lock.lock();
        co_ctx::running_co.erase(co);
        co_ctx::m_lock.unlock();
#endif
        /* finish run */
        co->stk_active_lock.unlock();
	}
}

void Scheduler::jump_to_sched(int arg)
{
    auto running = running_co;

    if (UNLIKELY(running == nullptr))
        swap_context(nullptr, &sched_ctx, arg);
    else
        swap_context(&running->ctx, &sched_ctx, arg);
}


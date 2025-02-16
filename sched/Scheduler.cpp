#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Scheduler.h"
#include "atomic_utils.h"

void Scheduler::push_to_ready(Co_t * co, bool enable_lock)
{
	std::unique_lock<spin_lock> lock;
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

void Scheduler::apply_ready(Co_t * co)
{
#ifdef __SCHED_CFS__
	if (co->sched.v_runtime == 0)
		co->sched.v_runtime = min_v_runtime.load(std::memory_order_acquire);

	sum_v_runtime += co->sched.v_runtime;
#endif

    co->scheduler = this;
    DEXPR( if (!co->sched.can_migration) )
        DASSERT(co->sched.occupy_thread == -1 || co->sched.occupy_thread == this_thread_id);

    co->sched.occupy_thread = this_thread_id;
	co->status_lock.lock();
	co->status = CO_READY;
	co->status_lock.unlock();

	push_to_ready(co, true);
	ready_count.fetch_add(1);
	sem_ready.signal();
}

void Scheduler::apply_ready_all(const std::vector<Co_t *> & co_vec)
{
	if (co_vec.empty())
		return;

#ifdef __SCHED_CFS__
	uint64_t cur_sum_v_runtime = 0;
#endif
	for (auto co : co_vec)
	{
#ifdef __SCHED_CFS__
		if (co->sched.v_runtime == 0)
			co->sched.v_runtime = min_v_runtime.load(std::memory_order_acquire);

        cur_sum_v_runtime += co->sched.v_runtime;
#endif
		co->status_lock.lock();
		co->status = CO_READY;
		co->status_lock.unlock();
        co->scheduler = this;
        co->sched.occupy_thread = this_thread_id;
	}

#ifdef __SCHED_CFS__
	sum_v_runtime.fetch_add(cur_sum_v_runtime, std::memory_order_acq_rel);
#endif

#ifdef __SCHED_CFS__
	sched_lock.lock();
#endif
	for (auto co : co_vec)
		push_to_ready(co, false);
#ifdef __SCHED_CFS__
    sched_lock.unlock();
#endif

	ready_count.fetch_add(co_vec.size());
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
		apply_ready_all(res);
		sem_ready.wait();
	}
	ready_count.fetch_sub(1);

    sched_lock.lock();
    DASSERT(!(ready.empty() && ready_fixed.empty()));
    sched_lock.unlock();

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
#ifdef __DEBUG_SCHED__
    co_ctx::m_lock.lock();
    co_ctx::co_vec.erase(ans);
    co_ctx::m_lock.unlock();
#endif
	return ans;
}

void Scheduler::run(Co_t * co) // 会丢失当前上下文，最后执行
{
	assert(UNLIKELY(co->status == CO_READY));

	co->status_lock.lock();
	co->sched.start_exec();
	if (LIKELY(co->id != co::MAIN_CO_ID)) // 设置 非main coroutine 栈帧
	{
#ifdef __STACK_STATIC__
		/* 设置stack分配器 */
		if (UNLIKELY(co->ctx.static_stk_pool == nullptr))
			co->ctx.static_stk_pool = &co_ctx::loc->alloc.stk_pool;

		/* 分配堆栈 */
		co->ctx.static_stk_pool->alloc_static_stk(co);
#elif __STACK_DYN__
		if (co->ctx.stk_dyn_alloc == nullptr) [[unlikely]]
			co_ctx::loc->alloc.dyn_stk_pool.alloc_stk(&co->ctx);
#endif
	}

	running_co = co;
	co->status = CO_RUNNING;
	co->sched.start_exec();
	co->status_lock.unlock();
	switch_context(&co->ctx);
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

	/* set interrupt coroutine */
	auto ans = running_co;
	running_co = nullptr;

	return ans;
}

uint64_t Scheduler::get_load()
{
    uint64_t load{};
#ifdef __SCHED_CFS__
    load += sum_v_runtime.load(std::memory_order_relaxed);
#endif
	load += ready_count.load(std::memory_order_relaxed) * 1000 * 1000;
	return load;
}

void Scheduler::coroutine_yield()
{
	/* interrupt coroutine */
	auto yield_co = interrupt(CO_INTERRUPT, false);
	remove_from_scheduler(yield_co);

	/* yield finish */
	yield_co->status_lock.unlock();
	
#ifdef __STACK_STATIC__
	/* release static stack */
	if (LIKELY(yield_co->ctx.static_stk_pool != nullptr))
		yield_co->ctx.static_stk_pool->release_stack(yield_co);
#endif

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

#ifdef __STACK_STATIC__
	/* release static stack */
	if (caller->ctx.static_stk_pool != nullptr) [[likely]]
		caller->ctx.static_stk_pool->release_stack(caller);
#endif
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

#ifdef __DEBUG_SCHED__
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
		int res = save_context(sched_ctx.get_jmp_buf(), &sched_ctx.first_full_save);
        DASSERT((uint64_t)sched_ctx.get_jmp_buf() > 1024 * 1024 * 1024);
        if (res != CONTEXT_CONTINUE)
		{
			process_callback(res);
			continue;
		}

		auto co = pickup_ready();
		run(co);
	}
}

void Scheduler::jump_to_sched(int arg)
{
	switch_context(&sched_ctx, arg);
}


//
// Created by hzj on 25-1-14.
//

#include <mutex>

#include "../include/CoPrivate.h"
#include "include/Sem_t.h"
#include "../sched/include/CfsSched.h"

void Sem_t::wait()
{
	if (wait_close) [[unlikely]]
		throw SemClosedException();

	caller_count--;

	lock.lock();
	count.fetch_sub(1, std::memory_order_release);
	if (count.load(std::memory_order_relaxed) >= 0)
	{
		lock.unlock();
		return;
	}
	wait_q.push(co_ctx::loc->scheduler->running_co);
	lock.unlock();

	// only running coroutine can use Sem
	// doesn't need to remove from scheduler
	auto co = co_ctx::loc->scheduler->running_co;
	co_ctx::loc->scheduler->interrupt();
	co->status = CO_WAITING;
	co_ctx::loc->scheduler->jump_to_exec();
}

bool Sem_t::try_wait()
{
	if (wait_close) [[unlikely]]
		throw SemClosedException();

	lock.lock();
	if (count.load(std::memory_order_acquire) <= 0)
	{
		lock.unlock();
		return false;
	}

	count.fetch_sub(1, std::memory_order_release);
	lock.unlock();
	return true;
}

void Sem_t::signal()
{
	if (caller_count == 0 && wait_close) [[unlikely]]
		throw SemClosedException();

	caller_count++;

	lock.lock();
	auto cnt = count.load(std::memory_order_acquire);
	count.fetch_add(1, std::memory_order_release);
	if (count.load(std::memory_order_acquire) > 0)
	{
		lock.unlock();
		return;
	}
	auto co = wait_q.front();
	wait_q.pop();
	lock.unlock();

	// push_back to scheduler
	co->status = CO_READY;
	co_ctx::manager->apply(co);

	/* resource release */
	if (cnt == 0 && wait_close) [[unlikely]]
		release(this);
}

void Sem_t::release(Sem_t * sem)
{
	sem->~Sem_t();
	if (sem->alloc) [[likely]]
		sem->alloc->free_safe(sem);
	else
		std::free(sem);
}

bool Sem_t::close()
{
	if (wait_close)
		return false;

	wait_close = true;
	std::lock_guard<spin_lock> lock_g(lock);
	// count >= 0: sem release now
	// count < 0: sem waiting to close
	return count >= 0;
}

void Sem_t::operator delete(void *ptr) noexcept
{
	// call the close, and doing nothing
	auto sem = static_cast<Sem_t*>(ptr);
	if (sem->close()) // close success
		release(sem);
	// close delay
	// memory release in member function
}

Sem_t::operator int32_t() { return count; }

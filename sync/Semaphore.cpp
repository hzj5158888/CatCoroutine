#include <cstdint>

#include "../include/CoCtx.h"
#include "../sched/include/Scheduler.h"
#include "include/Semaphore.h"
#include "include/Sem_t.h"

namespace co {
	void * sem_create(uint32_t count)
	{
		auto sem = static_cast<Sem_t*>(co_ctx::loc->alloc.sem_pool.allocate(sizeof (Sem_t)));
		if (UNLIKELY(sem == nullptr))
			return nullptr;

		new (sem) Sem_t(count);
		sem->alloc = &co_ctx::loc->alloc.sem_pool;
		return sem;
	}

	void sem_destroy(void * handle)
	{
        Sem_t::release(static_cast<Sem_t*>(handle));
	}

	void sem_wait(void * handle)
	{
		static_cast<Sem_t*>(handle)->wait();
	}

	bool sem_try_wait(void * handle)
	{
		return static_cast<Sem_t*>(handle)->try_wait();
	}

	void sem_signal(void * handle)
	{
		static_cast<Sem_t*>(handle)->signal();
	}

    int64_t sem_get_count(void * handle)
	{
		return static_cast<Sem_t*>(handle)->count;
	}
}
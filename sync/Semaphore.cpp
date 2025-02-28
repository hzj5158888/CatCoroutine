#include <cstdint>

#include "../include/CoCtx.h"
#include "../sched/include/Scheduler.h"
#include "include/Semaphore.h"
#include "include/Sem_t.h"

namespace co {
    void sem_construct(Sem_t * ptr, uint32_t count)
    {
        new (ptr) Sem_t(count);
    }

    Sem_t * sem_create(uint32_t count)
	{
		auto sem = static_cast<Sem_t*>(co_ctx::loc->alloc.sem_pool.allocate(sizeof (Sem_t)));
		if (UNLIKELY(sem == nullptr))
			return nullptr;

        sem_construct(sem, count);
		sem->alloc = &co_ctx::loc->alloc.sem_pool;
		return sem;
	}

	void sem_destroy(void * handle)
	{
        Sem_t::release(static_cast<Sem_t*>(handle));
	}
}
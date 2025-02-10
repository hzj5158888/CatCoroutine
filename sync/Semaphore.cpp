#include <cstdint>

#include "../include/CoCtx.h"
#include "../sched/include/CfsSched.h"
#include "include/Semaphore.h"
#include "include/Sem_t.h"
#include "../context/include/Context.h"

namespace co {
	void * sem_create(uint32_t count)
	{
		auto sem = static_cast<Sem_t*>(co_ctx::loc->alloc.sem_pool.allocate(sizeof (Sem_t)));
		if (sem == nullptr) [[unlikely]]
			return nullptr;

		new (sem) Sem_t(count);
		sem->alloc = &co_ctx::loc->alloc.sem_pool;
		return sem;
	}

	void sem_destroy(void * handle)
	{
		static_cast<Sem_t*>(handle)->close();
	}

	void sem_wait(void * handle)
	{
		auto co = co_ctx::loc->scheduler->running_co;
		int res = save_context(co->ctx.get_jmp_buf(), &co->ctx.first_full_save);
		if (res == CONTEXT_RESTORE)
			return;

		/* update stack size */
#ifdef __STACK_STATIC__
		co->ctx.set_stk_size();
#elif __STACK_DYN__
		co->ctx.set_stk_dyn_size();
#endif

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

	int32_t sem_get_count(void * handle)
	{
		return static_cast<Sem_t*>(handle)->count;
	}
}
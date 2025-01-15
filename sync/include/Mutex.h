//
// Created by hzj on 25-1-14.
//

#ifndef COROUTINE_SYNC_INCLUDE_MUTEX_H
#define COROUTINE_SYNC_INCLUDE_MUTEX_H

#include "Semaphore.h"

namespace co {
	class Mutex
	{
	private:
		Semaphore sem{};
	public:
		inline Mutex() = default;

		inline void lock() { sem.wait(); }
		inline bool try_lock() { return sem.try_wait(); }
		inline void unlock() { sem.signal(); }
	};
}

#endif //COROUTINE_SYNC_INCLUDE_MUTEX_H

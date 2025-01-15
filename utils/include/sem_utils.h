//
// Created by hzj on 25-1-14.
//

#pragma once

#include <cstdio>
#include <cstdlib>
#include <semaphore.h>
#include <cerrno>

namespace std {
	class CountingSemCreateException : public exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "counting_sem construct failed"; }
	};

	class counting_semaphore
	{
	private:
		sem_t sem{};
	public:
		explicit counting_semaphore(uint32_t count = 0);
		~counting_semaphore();
		inline void wait();
		inline void signal();
	};

	inline counting_semaphore::counting_semaphore(uint32_t count)
	{
		int res = sem_init(&sem, 0, count);
		if (res != 0)
			throw CountingSemCreateException();
	}

	inline counting_semaphore::~counting_semaphore()
	{
		sem_destroy(&sem);
	}

	inline void counting_semaphore::wait()
	{
		sem_wait(&sem);
	}

	inline void counting_semaphore::signal()
	{
		sem_post(&sem);
	}
}

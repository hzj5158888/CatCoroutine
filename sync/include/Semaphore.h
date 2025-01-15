//
// Created by hzj on 25-1-12.
//

#pragma once

#include <cstdint>
#include <memory>

#include "CoPrivate.h"
#include "MemoryPool.h"

namespace co {
	void * sem_create(uint32_t count);
	void sem_destroy(void * handle);
	void sem_wait(void * handle);
	bool sem_try_wait(void * handle);
	void sem_signal(void * handle);
	int32_t sem_get_count(void * handle);

	class SemaphoreCreateException : public std::exception
	{
	public:
		const char * what() const noexcept override { return "Coroutine Semaphore Create Failed"; }
	};
	class SemaphoreUnInitializationException : public std::exception
	{
	public:
		const char * what() const noexcept override { return "Coroutine Semaphore UnInitialization"; }
	};
	class SemaphoreDestroyException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Coroutine Semaphore Destroy Exception"; }
	};

	class Semaphore
	{
	private:
		void * handle{};
	public:
		inline Semaphore();
		inline explicit Semaphore(uint32_t x);
		inline Semaphore(const Semaphore & sem) = delete;
		inline Semaphore(Semaphore && sem) = default;
		inline ~Semaphore();

		inline void signal();
		inline void wait();
		inline bool try_wait();
		inline operator int32_t () const;
	};

	Semaphore::Semaphore()
	{
		handle = sem_create(0);
		if (handle == nullptr) [[unlikely]]
			throw SemaphoreCreateException();
	}

	Semaphore::Semaphore(uint32_t x)
	{
		handle = sem_create(x);
		if (handle == nullptr) [[unlikely]]
			throw SemaphoreCreateException();
	}

	Semaphore::~Semaphore() { sem_destroy(handle); handle = nullptr; }

	void Semaphore::signal()
	{
		if (handle == nullptr) [[unlikely]]
			throw SemaphoreUnInitializationException();

		sem_signal(handle);
	}

	void Semaphore::wait()
	{
		if (handle == nullptr) [[unlikely]]
			throw SemaphoreUnInitializationException();

		sem_wait(handle);
	}

	bool Semaphore::try_wait()
	{
		if (handle == nullptr) [[unlikely]]
			throw SemaphoreUnInitializationException();

		return sem_try_wait(handle);
	}

	Semaphore::operator int32_t() const { return sem_get_count(handle); }
}

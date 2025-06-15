//
// Created by hzj on 25-1-12.
//

#pragma once

#include <cstdint>
#include <memory>

#include "Sem_t.h"

namespace co {
    void sem_construct(Sem_t * ptr, uint32_t count);
	Sem_t * sem_create(uint32_t count);
	void sem_destroy(void * handle);

	class SemaphoreCreateException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Coroutine Semaphore Create Failed"; }
	};
	class SemaphoreUnInitializationException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Coroutine Semaphore UnInitialization"; }
	};
	class SemaphoreDestroyException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override { return "Coroutine Semaphore Destroy Exception"; }
	};

	class Semaphore
	{
	private:
        using callback_t = std::function<void(Co_t*)>;

        Sem_t * handle{};
	public:
		inline Semaphore();
		inline explicit Semaphore(uint32_t x);
		inline Semaphore(const Semaphore & sem) = delete;
		inline Semaphore(Semaphore && sem) noexcept ;
		inline ~Semaphore();

		inline void signal();
		inline void wait();
        inline void wait_then(const callback_t &);
        inline bool wait_for(std::chrono::microseconds duration);
        inline bool wait_for_then(std::chrono::microseconds duration, const callback_t & callback);
		inline bool try_wait();
		inline void swap(Semaphore && sem);
        [[nodiscard]] inline int64_t count() const;
		inline explicit operator int64_t () const;
	};

	Semaphore::Semaphore()
	{
        handle = sem_create(0);
		if (UNLIKELY(handle == nullptr))
			throw SemaphoreCreateException();
	}

	Semaphore::Semaphore(Semaphore && sem) noexcept { swap(std::move(sem)); }

	Semaphore::Semaphore(uint32_t x)
	{
		handle = sem_create(x);
		if (UNLIKELY(handle == nullptr))
			throw SemaphoreCreateException();
	}

	Semaphore::~Semaphore()
	{
		if (handle == nullptr)
			return;

		sem_destroy((void*)handle);
		handle = nullptr;
	}

	void Semaphore::signal()
	{
		if (UNLIKELY(handle == nullptr))
			throw SemaphoreUnInitializationException();

        handle->signal();
	}

	void Semaphore::wait()
	{
		if (UNLIKELY(handle == nullptr))
			throw SemaphoreUnInitializationException();

        handle->wait();
	}

    /* 只有唤醒了等待队列的协程时，才触发callback */
    void Semaphore::wait_then(const callback_t & callback)
    {
        if (UNLIKELY(handle == nullptr))
            throw SemaphoreUnInitializationException();

        handle->wait_then(callback);
    }

    bool Semaphore::wait_for(std::chrono::microseconds duration)
    {
        if (UNLIKELY(handle == nullptr))
            throw SemaphoreUnInitializationException();

        return handle->wait_for(duration);
    }

    /* 只有唤醒了等待队列的协程时，才触发callback */
    bool Semaphore::wait_for_then(std::chrono::microseconds duration, const callback_t & callback)
    {
        if (UNLIKELY(handle == nullptr))
            throw SemaphoreUnInitializationException();

        return handle->wait_for_then(duration, callback);
    }

	bool Semaphore::try_wait()
	{
		if (UNLIKELY(handle == nullptr))
			throw SemaphoreUnInitializationException();

		return handle->try_wait();
	}

	void Semaphore::swap(Semaphore && sem) { std::swap(handle, sem.handle); }

    int64_t Semaphore::count() const { return handle->count(); }

	Semaphore::operator int64_t() const { return handle->count(); }
}

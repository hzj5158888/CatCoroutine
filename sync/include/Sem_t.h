//
// Created by hzj on 25-1-14.
//
#pragma once

#include <queue>
#include <mutex>
#include <atomic>

#include "../../include/CoPrivate.h"
#include "../../allocator/include/MemoryPool.h"
#include "../../utils/include/spin_lock.h"

class SemClosedException : public std::exception
{
public:
	[[nodiscard]] const char * what() const noexcept override { return "Sem Closed Exception"; }
};

class Sem_t
{
private:
	static void release(Sem_t * ptr);
public:
	std::atomic<int32_t> count{};
	spin_lock lock{};
	std::queue<Co_t*> wait_q{};
	std::atomic<int32_t> caller_count{};
	std::atomic<bool> wait_close{false};

	MemoryPool * alloc{};

	explicit Sem_t(uint32_t val) : count(val) {};
	Sem_t(const Sem_t & sem) = delete;
	void wait();
	bool try_wait();
	void signal();
	bool close();

	explicit operator int32_t ();
	static void operator delete(void * ptr) noexcept;
};



//
// Created by hzj on 25-1-14.
//
#pragma once

#include <memory_resource>
#include <queue>
#include <mutex>
#include <atomic>

#include "../../include/CoPrivate.h"
#include "../../allocator/include/MemoryPool.h"
#include "../../utils/include/spin_lock.h"
#include "../../data_structure/include/QueueLock.h"
#include "../../data_structure/include/QuaternaryHeapLock.h"

class SemClosedException : public std::exception
{
public:
	[[nodiscard]] const char * what() const noexcept override { return "Sem Closed Exception"; }
};

class Sem_t
{
private:
    constexpr static auto MIN_SPIN = 2;
    constexpr static auto MAX_SPIN = 2000;
    constexpr static auto SPIN_LEVEL = 32;
    constexpr static auto HANDLE_QUEUE_CYCLE = 64;
    static_assert(HANDLE_QUEUE_CYCLE >= 32 && HANDLE_QUEUE_CYCLE <= 256);
    static_assert(((HANDLE_QUEUE_CYCLE - 1) & HANDLE_QUEUE_CYCLE) == 0);
public:
    uint32_t init_count{};
	std::atomic<int64_t> count{};
    std::atomic<int32_t> spinning_count{};
    std::atomic<int32_t> cur_max_spin{MAX_SPIN};
    std::atomic<uint16_t> cur_signal_spin_cycle{};
    std::array<QuaternaryHeapLock<Co_t*>, co::CPU_CORE> wait_q{};

#ifdef __MEM_PMR__
	std::pmr::synchronized_pool_resource * alloc{};
#else
	MemoryPool * alloc{};
#endif

	explicit Sem_t(uint32_t val) : count(val) { init_count = val; };
	Sem_t(const Sem_t & sem) = delete;
	void wait();
	bool try_wait();
	void signal();
    static void release(Sem_t * ptr);
    std::optional<Co_t*> try_pick_from_wait_q();

	explicit operator int64_t ();
    void operator delete (void * ptr) noexcept = delete;
};



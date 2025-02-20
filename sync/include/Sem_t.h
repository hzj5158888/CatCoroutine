//
// Created by hzj on 25-1-14.
//
#pragma once

#include <atomic>

#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/policies.hpp>
#include <cstdint>

#include "../../include/CoPrivate.h"
#include "../../allocator/include/MemoryPool.h"
#include "../../utils/include/spin_lock.h"
#include "../../data_structure/include/QueueLock.h"
#include "../../data_structure/include/QuaternaryHeapLock.h"
#include "../../utils/include/sem_utils.h"

using boost::lockfree::queue;
using boost::lockfree::capacity;

class SemClosedException : public std::exception
{
public:
	[[nodiscard]] const char * what() const noexcept override { return "Sem Closed Exception"; }
};

class SemOverflowException : public std::exception
{
public:
    [[nodiscard]] const char * what() const noexcept override { return "Sem Overflow Exception"; }
};

class Sem_t
{
private:
    constexpr static auto MIN_SPIN = 2;
    constexpr static auto MAX_SPIN = 64;
    constexpr static auto SPIN_LEVEL = 8;
    constexpr static auto COUNT_MASK = 0xffffffff;
    constexpr static auto WAITER_SHIFT = 32;
    constexpr static auto QUEUE_NUMBER = co::CPU_CORE * 2;
    static_assert(((QUEUE_NUMBER - 1) & QUEUE_NUMBER) == 0);
public:
    uint32_t init_count{};
	std::atomic<uint64_t> m_value{};
    alignas(__CACHE_LINE__) std::atomic<int32_t> max_spin{MAX_SPIN};
    std::array<QuaternaryHeapLock<Co_t*>, QUEUE_NUMBER> wait_q{};
    std::atomic<uint16_t> q_push_idx{};
    std::atomic<uint16_t> q_pop_idx{};
    std::atomic<int32_t> m_q_size{};

#ifdef __MEM_PMR__
	std::pmr::synchronized_pool_resource * alloc{};
#else
	MemoryPool * alloc{};
#endif

	explicit Sem_t(uint32_t val) : m_value(val) { init_count = val; };
	Sem_t(const Sem_t & sem) = delete;
	void wait();
	bool try_wait();
	void signal();
    static void release(Sem_t * ptr);
    Co_t * try_pick_from_wait_q();
    void inc_max_spin();
    void dec_max_spin();

    int64_t count();
	explicit operator int64_t ();
    void operator delete (void * ptr) noexcept = delete;
};



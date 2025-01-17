//
// Created by hzj on 25-1-9.
//
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <queue>
#include <atomic>
#include <set>

#include "../context/include/Context.h"
#include "../include/Coroutine.h"
#include "../utils/include/co_utils.h"
#include "../utils/include/Result.h"
#include "../data_structure/include/RingBuffer.h"
#include "../allocator/include/MemoryPool.h"
#include "../../sched/include/CfsSchedDef.h"
#include "../../sync/include/Mutex.h"
#include "AllocatorGroup.h"

enum CO_STATUS
{
    CO_NEW = 1,
    CO_READY,
    CO_RUNNING,
    CO_WAITING,
    CO_CLOSING,
	CO_DEAD
};

struct Co_t
{
    uint32_t id{co::INVALID_CO_ID};
    std::atomic<uint8_t> status{CO_NEW};

	// 状态锁
	spin_lock status_lock{};

	// 线程所有权
	spin_lock stk_active_lock{};

	// 分配器信息
	MemoryPool * allocator{};

    // 上下文
    Context ctx{};

    // 存储
	// CFS Scheduler
	using CoReadyIter = std::multiset<Co_t*>::iterator;
	CoReadyIter ready_self{};

	// await
	spin_lock await_lock{};
	Co_t * await_callee{}; // await 谁
	std::queue<Co_t *> await_caller{}; // 谁 await

    // 调度信息
	std::shared_ptr<CfsSchedEntity> sched{};
	std::shared_ptr<CfsScheduler> scheduler{};

	Co_t();
	Co_t(const Co_t & oth) = delete;
	Co_t(Co_t && oth) = delete;
	~Co_t() = default;

    void set_id(uint32_t co_id) { id = co_id; }

	bool operator > (const Co_t & oth) const;
	bool operator < (const Co_t & oth) const;
	static void operator delete (void * ptr) noexcept
	{
		Co_t * co = static_cast<Co_t*>(ptr);
		auto alloc = co->allocator;
		if (alloc)
			alloc->free_safe(co);
		else
			std::free(co);
	}
};

template<>
struct std::hash<Co_t>
{
	std::size_t operator() (const Co_t & oth) const { return oth.id; }
};

struct CoPtrLessCmp
{
	bool operator () (const Co_t * a, const Co_t * b) const { return *a < *b; }
};

struct CoPtrGreaterCmp
{
	bool operator () (const Co_t * a, const Co_t * b) const { return *a > *b; }
};

struct local_t
{
	std::thread::id thread_id{};
	std::unique_ptr<AllocatorGroup> alloc{};
	std::shared_ptr<CfsScheduler> scheduler{};
};

namespace co_ctx
{
	extern bool is_init;
	extern std::unique_ptr<Context> manger_ctx;
	extern std::shared_ptr<CfsSchedManager> manager;
	extern std::atomic<uint32_t> coroutine_count;
	extern std::vector<Co_t*> co_vec; // debug
	extern thread_local std::shared_ptr<local_t> loc;
}
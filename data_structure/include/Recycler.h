#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>
#include <atomic>
#include <queue>

#include "../utils/include/utils.h"
#include "../utils/include/spin_lock.h"

#include "./moodycamel/ConcurrentQueue.h"

using moodycamel::ConcurrentQueue;
using moodycamel::ConcurrentQueueDefaultTraits;

template<typename NodeType, typename Allocator, size_t LIFE_CYCLE = 8192>
class Recycler
{
private:
    constexpr static size_t MAX_QUEUE_SIZE = 3 * LIFE_CYCLE / 2;
    constexpr static size_t TRY_RECYCLE_SIZE = 4 * LIFE_CYCLE / 3;

    class info
    {
    public:
        uint64_t time{};
        NodeType * node{};

        info() = default;
        info(const info & oth) : time(oth.time), node(oth.node) {}
        info(const uint64_t time, NodeType * node) : time(time), node(node) {}
    };

    Allocator alloc{};
    std::atomic<uint64_t> time_stamp{};
    std::vector<ConcurrentQueue<info>> pool{};
public:
    Recycler() = delete;
    explicit Recycler(const size_t max_type_count)
    {
        pool.resize(max_type_count + 1);
    }

    ~Recycler()
    {
        for (auto & q : pool)
        {
            while (q.size_approx())
            {
                info node_info{};
                auto res = q.try_dequeue(node_info);
                if (!res)
                    break;

                NodeType::destroy(alloc, node_info.node);
            }
        }
    }

    void pool_add_node(const size_t type, const info & node_info)
    {
        auto & q = pool[type];

        /* true => 插入成功，退出循环 */
        /* false => 插入失败，继续循环 */
        while (!q.try_enqueue(node_info)) // Fails if not enough memory to enqueue
        {
            info free_node{};
            auto res = q.try_dequeue(free_node);
            if (res)
                NodeType::destroy(alloc, free_node.node);
        }
    }

    template<typename ... Args>
    NodeType * allocate(const size_t type, Args &&... args)
    {
        auto needToDelete = [this] (const info & x) -> bool
        {
            return this->time_stamp.load(std::memory_order_acquire) - x.time > LIFE_CYCLE;
        };

        info node_info{};
        if (pool[type].size_approx() > TRY_RECYCLE_SIZE && pool[type].try_dequeue(node_info))
        {
            if (!needToDelete(node_info))
            {
                pool_add_node(type, node_info);
                return NodeType::create(alloc, std::forward<Args>(args)...);
            }

            new (node_info.node) NodeType(std::forward<Args>(args)...);
            return node_info.node;
        }

        return NodeType::create(alloc, std::forward<Args>(args)...);
    }

    void deallocate(size_t type, NodeType * node)
    {
        uint64_t cur_time_stamp = time_stamp.fetch_add(1, std::memory_order_release);
        pool_add_node(type, info{cur_time_stamp, node});
    }
};
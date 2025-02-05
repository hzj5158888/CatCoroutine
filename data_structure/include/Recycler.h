#pragma once

#include <boost/lockfree/policies.hpp>
#include <cstddef>
#include <array>
#include <atomic>

#include <boost/lockfree/queue.hpp>
#include <mutex>
#include <queue>

#include "../utils/include/spin_lock.h"

template<typename NodeType, typename Allocator, size_t TYPE_COUNT, int LIFE_CYCLE = 128 * 1024>
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
        info(const uint64_t time, NodeType * node) : time(time), node(node) {}
    };

    Allocator alloc{};
    std::atomic<uint64_t> time_stamp{};
#ifdef __RECYCLER_LF__
    std::array<boost::lockfree::queue<info, boost::lockfree::capacity<MAX_QUEUE_SIZE>>, TYPE_COUNT + 1> pool{};
#else
    struct lock_queue
    {
        std::queue<info> m_q{};
        spin_lock m_lock{};

        lock_queue() = default;
        ~lock_queue() = default;
    };

    std::array<lock_queue, TYPE_COUNT + 1> pool{};
#endif

public:
    explicit Recycler() = default;

    ~Recycler()
    {
#ifdef __RECYCLER_LF__
        for (auto & q : pool)
#else
        for (auto & [q, m_lock] : pool)
#endif
        {
            while (!q.empty())
            {
                info node_info{};
#ifdef __RECYCLER_LF__
                auto res = q.pop(node_info);
                if (!res)
                    break;
#else
                node_info = q.front();
                q.pop();
#endif
                NodeType::destroy(alloc, node_info.node);
            }
        }
    }

    bool needToDelete(const info & x)
    {
        return this->time_stamp.load(std::memory_order_acquire) - x.time > LIFE_CYCLE;
    }

    void pool_add_node(const size_t type, const info & node_info)
    {
#ifdef __RECYCLER_LF__
        auto & q = pool[type];
        /* true => 插入成功，退出循环 */
        /* false => 插入失败，继续循环 */
        while (!q.push(node_info)) // Fails if not enough memory to enqueue
        {
            info free_node{};
            auto res = q.pop(free_node);
            if (res)
                NodeType::destroy(alloc, free_node.node);
        }
#else
        auto & [q, m_lock] = pool[type];
        std::lock_guard<spin_lock> lock(m_lock);
        q.push(node_info);
#endif
    }

    template<typename ... Args>
    NodeType * allocate(const size_t type, Args &&... args)
    {
#ifdef __RECYCLER_LF__
        info node_info{};
        if (pool[type].pop(node_info))
        {
            if (!needToDelete(node_info))
            {
                pool_add_node(type, node_info);
            } else {
                new (node_info.node) NodeType(std::forward<Args>(args)...);
                return node_info.node;
            }
        }
#else
        auto & [q, m_lock] = pool[type];
        m_lock.lock();
        if (q.size() > TRY_RECYCLE_SIZE && needToDelete(q.front()))
        {
            info node_info = q.front();
            q.pop();
            m_lock.unlock();

            new (node_info.node) NodeType(std::forward<Args>(args)...);
            return node_info.node;
        }
        m_lock.unlock();
#endif
        return NodeType::create(alloc, std::forward<Args>(args)...);
    }

    void deallocate(size_t type, NodeType * node)
    {
        uint64_t cur_time_stamp = time_stamp.fetch_add(1, std::memory_order_seq_cst);
        pool_add_node(type, info{cur_time_stamp, node});
    }
};
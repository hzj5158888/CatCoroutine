#pragma once

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

namespace co {

    template<typename NodeType, typename Allocator>
    class NodeRecycler {
    private:
        constexpr static size_t MAX_NODE_LIFE_CYCLE = 10000;
        constexpr static size_t MAX_QUEUE_SIZE = 3 * MAX_NODE_LIFE_CYCLE / 2;
        constexpr static size_t TRY_RECYCLE_SIZE = 4 * MAX_NODE_LIFE_CYCLE / 3;

        class info {
        public:
            uint64_t time{};
            NodeType *node{};

            info() = default;

            info(const info &oth) : time(oth.time), node(oth.node) {}

            info(const uint64_t time, NodeType *node) : time(time), node(node) {}
        };

        spin_lock m_lock{};
        std::atomic<uint64_t> time_stamp{};
        Allocator alloc{};
        std::vector<ConcurrentQueue<info>> pool{};
    public:
        NodeRecycler() = delete;

        explicit NodeRecycler(const size_t max_height) {
            pool.resize(max_height + 1);
        }

        ~NodeRecycler() {
            for (auto &q: pool) {
                while (q.size_approx()) {
                    info node_info{};
                    auto res = q.try_dequeue(node_info);
                    if (!res)
                        break;

                    NodeType::destroy(alloc, node_info.node);
                }
            }
        }

        void pool_add_node(const uint8_t height, const info &node_info) {
            auto &q = pool[height];

            /* true => 插入成功，退出循环 */
            /* false => 插入失败，继续循环 */
            while (!q.try_enqueue(node_info)) {
                info free_node{};
                auto res = q.try_dequeue(free_node);
                if (res)
                    NodeType::destroy(alloc, free_node.node);
            }
        }

        template<typename ... Args>
        NodeType *allocate(const uint8_t height, Args &&... args) {
            auto needToDelete = [this](const info &x) -> bool {
                return this->time_stamp.load(std::memory_order_acquire) - x.time > MAX_NODE_LIFE_CYCLE;
            };

            info node_info{};
            if (pool[height].size_approx() > TRY_RECYCLE_SIZE && pool[height].try_dequeue(node_info)) {
                if (!needToDelete(node_info)) {
                    pool_add_node(height, node_info);
                    return NodeType::create(alloc, height, std::forward<Args>(args)...);
                }

                new(node_info.node) NodeType(height, std::forward<Args>(args)...);
                return node_info.node;
            }

            return NodeType::create(alloc, height, std::forward<Args>(args)...);
        }

        void free(NodeType *node) {
            uint8_t height = node->height;
            uint64_t cur_time_stamp = time_stamp.fetch_add(1, std::memory_order_release);
            pool_add_node(height, info{cur_time_stamp, node});
        }
    };
}
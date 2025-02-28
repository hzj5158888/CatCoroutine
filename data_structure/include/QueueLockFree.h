#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <optional>

#include "../utils/include/atomic_utils.h"
#include "../utils/include/spin_lock.h"
#include "../allocator/include/PmrAllocator.h"

namespace co {
    template<typename T, typename Allocator = std::allocator<uint8_t>>
    class QueueLockFree {
    private:
        using spin_lock_type = spin_lock;

        struct Node {
            enum : uint16_t {
                REMOVAL = (1 << 0),
            };

            T data;
            std::atomic<Node *> next{};
            std::atomic<uint8_t> m_flag{};
            spin_lock_type alter_lock{};

            ~Node() = default;

            static Node *create(Allocator &alloc) {
                Node *ans = reinterpret_cast<Node *>(alloc.allocate(sizeof(Node)));
                new(&ans->next) std::atomic<Node *>();
                new(&ans->alter_lock) spin_lock_type();
                new(&ans->m_flag) std::atomic<uint8_t>();
                return ans;
            }

            static void destroy(Allocator &alloc, Node *node) {
                node->~Node();
                alloc.deallocate(reinterpret_cast<uint8_t *>(node), sizeof(Node));
            }

            uint8_t flag() const { return m_flag.load(std::memory_order_acquire); }

            void setFlag(uint8_t f) { m_flag.store(f, std::memory_order_release); }

            bool removal() const { return flag() & REMOVAL; }

            void setRemoval() { setFlag(flag() | REMOVAL); }

            void clearRemoval() { setFlag(flag() & ~REMOVAL); }

            std::unique_lock<spin_lock_type> acquireLock() { return std::unique_lock(alter_lock); }

            std::unique_lock<spin_lock_type> tryLock() { return std::unique_lock(alter_lock, std::try_to_lock); }
        };

        std::atomic<Node *> head{}, tail{};
        std::atomic<size_t> m_size{0};
        Allocator alloc{};
    public:
        QueueLockFree() {
            head = tail = Node::create(alloc);
        }

        ~QueueLockFree() {
            Node *cur = head;
            while (cur) {
                Node *next = cur->next;
                Node::destroy(alloc, cur);
                cur = next;
            }

            m_size = 0;
            head = tail = nullptr;
        }

        template<typename V>
        void push(V data) {
            Node *cur = Node::create(alloc);
            cur->data = std::forward<V>(data);

            Node *cur_tail{};
            while (true) {
                cur_tail = tail.load(std::memory_order_acquire);
                if (UNLIKELY(cur_tail->removal()))
                    continue;

                Node *expected_next = nullptr;
                if (LIKELY(cur_tail->next.compare_exchange_weak(expected_next, cur, std::memory_order_acq_rel))) {
                    tail.store(cur, std::memory_order_seq_cst);
                    break;
                }
            }

            m_size.fetch_add(1, std::memory_order_acq_rel);
        }

        std::optional<T> try_pop() {
            Node *cur_head = head.load(std::memory_order_relaxed);

            Node *nodeToBeDel{};
            auto pop_fn = [&nodeToBeDel](Node *cur) -> Node * {
                if (cur == nullptr)
                    return nullptr;

                nodeToBeDel = cur;
                cur->setRemoval();
                return cur->next.load(std::memory_order_acquire);
            };
            atomic_fetch_modify(cur_head->next, pop_fn, std::memory_order_acq_rel);

            if (nodeToBeDel == nullptr)
                return std::nullopt;

            auto tmp_self = nodeToBeDel;
            tail.compare_exchange_weak(tmp_self, cur_head, std::memory_order_acq_rel);
            std::optional<T> ans{std::move(nodeToBeDel->data)};
            Node::destroy(alloc, nodeToBeDel);
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            return ans;
        }

        [[nodiscard]] int32_t size() const { return m_size.load(std::memory_order_acquire); }

        [[nodiscard]] bool empty() const { return size() == 0; }
    };
}
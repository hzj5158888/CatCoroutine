#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "../utils/include/spin_lock.h"

template<typename T, typename Allocator = std::allocator<uint8_t>>
class QueueLockFree
{
private:
    using spin_lock_type = spin_lock;

    struct Node
    {
        enum : uint16_t
        {
            REMOVAL = (1 << 0),
            FULLY_LINKED = (1 << 1),
        };

        T data;
        std::atomic<Node *> next{};
        std::atomic<uint8_t> m_flag{};
        spin_lock_type alter_lock{};

        ~Node() = default;

        static Node * create(Allocator & alloc)
        {
            Node * ans = reinterpret_cast<Node*>(alloc.allocate(sizeof(Node)));
            new (&ans->next) std::atomic<Node*>();
            new (&ans->alter_lock) spin_lock_type();
            new (&ans->m_flag) std::atomic<uint8_t>();
            return ans;
        }

        static void destroy(Allocator & alloc, Node * node)
        {
            node->~Node();
            alloc.deallocate(reinterpret_cast<uint8_t*>(node), sizeof(Node));
        }

        Node * getValidNext()
        {
            if (!validNode(this))
                return nullptr;

            Node * v_next = next.load(std::memory_order_acquire);
            while (v_next && v_next->removal())
                v_next = next.load(std::memory_order_relaxed);

            return v_next;
        }

        uint8_t flag() const { return m_flag.load(std::memory_order_acquire); }
        void setFlag(uint8_t f) { m_flag.store(f, std::memory_order_release); }
        bool removal() const { return flag() & REMOVAL; }
        void setRemoval() { setFlag(flag() | REMOVAL); }
        void clearRemoval() { setFlag(flag() & ~REMOVAL); }
        std::unique_lock<spin_lock_type> acquireLock() { return std::unique_lock(alter_lock); }
        std::unique_lock<spin_lock_type> tryLock() { return std::unique_lock(alter_lock, std::try_to_lock); }
    };

    static bool validNode(Node * node)
    {
        return node && !node->removal();
    }

    bool lockForAlter(
        Node * pred, 
        Node * self, 
        std::unique_lock<spin_lock_type> & pred_guard)
    {
        pred_guard = pred->acquireLock();
        if (pred->removal() || pred->next.load(std::memory_order_acquire) != self)
            return false;
        
        return true;
    }

    /* 头插法 */
    std::atomic<Node *> head{}, tail{};
    std::atomic<size_t> m_size{0};
    Allocator alloc{};
public:
    QueueLockFree()
    {
        head = tail = Node::create(alloc);
    }

    ~QueueLockFree()
    {
        Node * cur = head;
        while (cur)
        {
            cur->alter_lock.lock();
            Node * next = cur->next;
            Node::destroy(alloc, cur);
            cur = next;
        }

        m_size = 0;
        head = tail = nullptr;
    }

    template<typename V>
    void push_back(V data)
    {
        Node * cur = Node::create(alloc);
        cur->data = std::forward<V>(data);

        Node * cur_tail{};
        while (true)
        {
            cur_tail = tail.load(std::memory_order_acquire);
            std::unique_lock<spin_lock_type> guard = cur_tail->tryLock();
            if (!guard.owns_lock())
                continue;
                
            if (cur_tail->next.load(std::memory_order_acquire) == nullptr && !cur_tail->removal())
            {
                cur_tail->next.store(cur, std::memory_order_release);
                tail.store(cur, std::memory_order_release);
                break;
            }
        }

        m_size.fetch_add(1, std::memory_order_acq_rel);
    }

    std::optional<T> try_pop()
    {
        while (true)
        {
            if (empty())
                return std::nullopt;

            Node * cur_head = head.load(std::memory_order_acquire);
            Node * self = cur_head->next.load(std::memory_order_relaxed);
            
            bool isMarked = false;
            Node * nodeToDel = nullptr;
            std::unique_lock<spin_lock_type> self_guard;
            while (true)
            {
                if (!isMarked && !validNode(self))
                    break;

                if (!isMarked)
                {
                    nodeToDel = self;
                    self_guard = nodeToDel->acquireLock();
                    if (self->removal())
                        break;

                    nodeToDel->setRemoval();
                    isMarked = true;
                }

                std::unique_lock<spin_lock_type> pred_guard;
                if (!lockForAlter(cur_head, self, pred_guard))
                {
                    isMarked = false;
                    break;
                }

                Node * succ = self->next.load(std::memory_order_acquire);
                cur_head->next.store(succ, std::memory_order_release);

                /* 如果删除尾节点 */
                if (nodeToDel == tail.load(std::memory_order_acquire))
                    tail.store(cur_head, std::memory_order_release);

                break;
            }

            if (!isMarked)
                continue;

            std::optional<T> ans{std::move(nodeToDel->data)};
            Node::destroy(alloc, nodeToDel);
            m_size.fetch_sub(1, std::memory_order_seq_cst);
            return ans;
        }
    }

    int32_t size() const { return m_size.load(std::memory_order_acquire); }
    bool empty() const { return size() == 0; }
};
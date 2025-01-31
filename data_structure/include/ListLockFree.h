#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "./Recycler.h"

#include "../utils/include/spin_lock.h"

template<typename T, typename Allocator = std::allocator<uint8_t>>
class ListLockFree
{
private:
    using spin_lock_type = spin_lock;

    constexpr static uint8_t NODE_TYPE = 1;

    struct Node
    {
        enum : uint16_t
        {
            REMOVAL = (1 << 0),
            FULLY_LINKED = (1 << 1),
        };

        T data;
        std::atomic<Node *> next{};
        std::atomic<Node *> pred{};
        std::atomic<uint8_t> m_flag{};
        spin_lock_type alter_lock{};

        ~Node() = default;

        static Node * create(Allocator & alloc)
        {
            Node * ans = reinterpret_cast<Node*>(alloc.allocate(sizeof(Node)));
            new (&ans->next) std::atomic<Node*>();
            new (&ans->pred) std::atomic<Node*>();
            new (&ans->alter_lock) spin_lock();
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
                v_next = next.load(std::memory_order_acquire);

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
        std::unique_lock<spin_lock_type> & pred_guard,
        std::unique_lock<spin_lock_type> & succ_guard)
    {
        pred_guard = pred->acquireLock();
        if (pred->removal() || pred->next.load(std::memory_order_acquire) != self)
            return false;

        Node * succ = self->next.load(std::memory_order_acquire);
        if (succ != nullptr)
        {
            succ_guard = succ->tryLock();
            if (!succ_guard.owns_lock() || succ->removal() || succ->pred.load(std::memory_order_acquire) != self)
                return false;
        }
        
        return true;
    }

    /* 头插法 */
    std::atomic<Node *> head{}, tail{};
    std::atomic<size_t> m_size{0};
    Recycler<Node, Allocator> recycler{1};
public:
    /* 迭代器存储目标节点的 **上一个** 节点 */
    class iterator
    {
    public:
        Node * self{};
        spin_lock_type m_lock{};

        iterator() = default;
        explicit iterator(Node * self) : self(self) {}
        iterator(iterator && oth) = default;
        iterator(const iterator & oth) = default;

        std::atomic<Node*> * atomic_self() { return reinterpret_cast<std::atomic<Node*> *>(&self); }
        std::atomic<Node*> const * atomic_self() const { return reinterpret_cast<std::atomic<Node*> const *>(&self); }

        bool valid() const
        { 
            return validNode(atomic_self()->load(std::memory_order_acquire));
        }

        std::unique_lock<spin_lock_type> tryLock()
        {
            return std::unique_lock(m_lock, std::try_to_lock);
        }

        T & operator -> () { return atomic_self()->load(std::memory_order_acquire)->data; }
        T & operator * () { return self->data; }

        const iterator & operator ++ ()
        {
            atomic_self()->store(atomic_self()->load(std::memory_order_acquire)->getValidNext(), std::memory_order_release);
            return *this;
        }

        iterator operator ++ (int)
        {
            iterator old = *this;
            *this = ++(*this);
            return old;
        }

        void operator = (const iterator & oth)
        {
            atomic_self()->store(oth.atomic_self()->load(std::memory_order_acquire), std::memory_order_release);
        }
    };

    ListLockFree()
    {
        head = tail = recycler.allocate(NODE_TYPE);
    }

    ~ListLockFree()
    {
        Node * cur = head;
        while (cur)
        {
            cur->alter_lock.lock();
            Node * next = cur->next;
            recycler.deallocate(NODE_TYPE, cur);
            cur = next;
        }

        m_size = 0;
        head = tail = nullptr;
    }

    template<typename V>
    iterator push_back(V data)
    {
        Node * cur = recycler.allocate(NODE_TYPE);
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
                cur->pred.store(cur_tail, std::memory_order_release);
                cur_tail->next.store(cur, std::memory_order_release);
                tail.store(cur, std::memory_order_release);
                break;
            }
        }

        iterator ans{cur};
        m_size.fetch_add(1, std::memory_order_acq_rel);
        return ans;
    }

    std::optional<T> try_pop_front()
    {
        while (true)
        {
            if (empty())
                return std::nullopt;

            Node * cur_head = head.load(std::memory_order_relaxed);
            Node * self = cur_head->next.load(std::memory_order_acquire);
            
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

                std::unique_lock<spin_lock_type> pred_guard, succ_guard;
                if (!lockForAlter(cur_head, self, pred_guard, succ_guard))
                {
                    nodeToDel->clearRemoval();
                    isMarked = false;
                    break;
                }

                Node * succ = self->next.load(std::memory_order_acquire);
                cur_head->next.store(succ, std::memory_order_release);
                if (succ != nullptr)
                    succ->pred.store(cur_head, std::memory_order_release);

                /* 如果删除尾节点 */
                if (nodeToDel == tail.load(std::memory_order_acquire))
                    tail.store(cur_head, std::memory_order_release);

                break;
            }

            if (!isMarked)
                continue;

            std::optional<T> ans{std::move(nodeToDel->data)};
            recycler.deallocate(NODE_TYPE, nodeToDel);
            m_size.fetch_sub(1, std::memory_order_acq_rel);
            return ans;
        }
    }

    bool try_erase(Node * self)
    {
        bool isMarked = false;
        Node * nodeToDel = nullptr;
        std::unique_lock<spin_lock_type> self_guard;
        while (true)
        {
            if (!isMarked && !validNode(self))
                return false;

            if (!isMarked)
            {
                nodeToDel = self;
                self_guard = nodeToDel->acquireLock();
                if (self->removal())
                    return false;

                nodeToDel->setRemoval();
                isMarked = true;
            }

            Node * pred = nodeToDel->pred.load(std::memory_order_acquire);
            std::unique_lock<spin_lock_type> pred_guard, succ_guard;
            if (!lockForAlter(pred, nodeToDel, pred_guard, succ_guard))
            {
                self_guard.unlock();
                self_guard.release();
                nodeToDel->clearRemoval();
                isMarked = false;
                continue;
            }

            Node * succ = nodeToDel->next.load(std::memory_order_acquire);
            pred->next.store(succ, std::memory_order_release);
            if (succ != nullptr)
                succ->pred.store(pred, std::memory_order_release);

            /* 如果删除尾节点 */
            if (nodeToDel == tail.load(std::memory_order_acquire))
                tail.store(pred, std::memory_order_release);

            break;
        }

        recycler.deallocate(NODE_TYPE, nodeToDel);
        m_size.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    bool try_erase(iterator iter)
    {
        return try_erase(iter.self);
    }

    iterator begin() 
    { 
        Node * cur_head = head.load(std::memory_order_relaxed);
        return iterator(cur_head->getValidNext());
    }

    iterator end() const { return iterator(); }
    int32_t size() const { return m_size.load(std::memory_order_acquire); }
    bool empty() const { return size() == 0; }
};
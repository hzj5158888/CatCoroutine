#pragma once

#include <algorithm>
#include <memory>
#include <random>
#include <cassert>
#include <optional>
#include <mutex>

#include "../utils/include/spin_lock.h"
#include "../utils/include/utils.h"

#include "SkipListNodeRecycler.h"

namespace co {
    template<typename T, typename F = std::less<T>, typename Allocator = std::allocator<uint8_t>>
    class SkipListLockFree {
    private:
        constexpr static auto MAX_HEIGHT = 32;

        constexpr static size_t sizeLimit[32] = {
                1, 2, 4, 8, 16,
                32, 64, 128, 256, 512,
                1024, 2048, 4096, 8192, 16384,
                32768, 65536, 131072, 262144, 524288,
                1048576, 2097152, 4194304, 8388608, 16777216,
                33554432, 67108864, 134217728, 268435456, 536870912,
                1073741824, 2147483648
        };

        using spin_lock_type = spin_lock;

        class Node {
            enum : uint16_t {
                IS_HEAD_NODE = 1,
                MARKED_FOR_REMOVAL = (1 << 1),
                FULLY_LINKED = (1 << 2),
            };
        public:
            std::atomic<uint16_t> m_flags{};
            const uint8_t height;
            std::atomic<uint32_t> ref_count{0};
            spin_lock_type m_lock{};

            std::optional<T> key;

            explicit Node(const uint8_t height) : height(height) { construct(false); }

            explicit Node(const uint8_t height, const T &k) : height(height), key(k) { construct(false); }

            explicit Node(const uint8_t height, const T &k, bool isHead, uint32_t cnt) : key(k), height(height),
                                                                                         ref_count(cnt) {
                construct(isHead);
            }

            explicit Node(const uint8_t height, T &&k, bool isHead, uint32_t cnt) : key(std::move(k)), height(height),
                                                                                    ref_count(cnt) {
                construct(isHead);
            }

            explicit Node(const uint8_t height, T &&k) : height(height), key(std::move(k)) { construct(false); }

            ~Node() {
                for (uint8_t i = 0; i < height; i++)
                    m_skip[i].~atomic();
            }

            Node *skip(uint8_t level) {
                DASSERT(level < height);
                return m_skip[level].load(std::memory_order_acquire);
            }

            void setSkip(uint8_t level, Node *x) {
                DASSERT(level < height);
                m_skip[level].store(x, std::memory_order_release);
            }

            /* 循环下一个有效的节点 */
            Node *next(bool unlock_exit = true) {
                Node *node;
                for (node = skip(0); (node != nullptr && node->markedForRemoval()); node = node->skip(0))
                    __builtin_ia32_pause();

                return node;
            }

            Node *copyHead(Node *node) {
                DASSERT(node != nullptr && height > node->height);
                setFlags(node->flags());
                for (uint8_t i = 0; i < node->height; i++)
                    setSkip(i, node->skip(i));

                return this;
            }

            static void destroy(Allocator &alloc, Node *node) {
                auto height = node->height;
                node->~Node();
                alloc.deallocate(reinterpret_cast<uint8_t *>(node),
                                 sizeof(Node) + sizeof(std::atomic<void *>) * height);
            }

            template<typename ... Args>
            static Node *create(Allocator &alloc, const uint8_t height, Args &&... args) {
                Node *ans = reinterpret_cast<Node *>(alloc.allocate(
                        sizeof(Node) + sizeof(std::atomic<void *>) * height));
                new(ans) Node(height, args...);
                return ans;
            }

            std::unique_lock<spin_lock_type> acquireLock() { return std::unique_lock(m_lock); }

            [[nodiscard]] uint16_t flags() const { return m_flags.load(std::memory_order_acquire); }

            void setFlags(uint16_t f) { return m_flags.store(f, std::memory_order_release); }

            [[nodiscard]] bool fullyLinked() const { return flags() & FULLY_LINKED; }

            [[nodiscard]] bool markedForRemoval() const { return flags() & MARKED_FOR_REMOVAL; }

            [[nodiscard]] bool isHeadNode() const { return flags() & IS_HEAD_NODE; }

            void setIsHeadNode() { setFlags(uint16_t(flags() | IS_HEAD_NODE)); }

            void setFullyLinked() { setFlags(uint16_t(flags() | FULLY_LINKED)); }

            void setMarkedForRemoval() { setFlags(uint16_t(flags() | MARKED_FOR_REMOVAL)); }

            void clearMarkedForRemoval() { setFlags(uint16_t(flags() & (~MARKED_FOR_REMOVAL))); }

            uint32_t refCount() { return ref_count.load(std::memory_order_acquire); }

            void setRefCount(uint32_t x) { ref_count.store(x, std::memory_order_release); }

            uint32_t incRefCount(uint32_t inc) { return ref_count.fetch_add(inc, std::memory_order_acq_rel) + inc; }

            uint32_t decRefCount(uint32_t dec) { return ref_count.fetch_sub(dec, std::memory_order_acq_rel) - dec; }

        private:
            void construct(bool isHead) {
                m_flags = 0;
                if (isHead)
                    setIsHeadNode();

                for (uint8_t i = 0; i < height; i++)
                    new(&m_skip[i]) std::atomic<Node *>(nullptr);
            }
            /* m_next定义在最后 */
            /* 类似柔性数组 */
            std::atomic<Node *> m_skip[0];
        };

        const F cmp{};
        NodeRecycler<Node, Allocator> recycler{MAX_HEIGHT};

        std::atomic<Node *> head{};
        std::atomic<size_t> m_size{};
        std::uniform_int_distribution<> dis{1, INT32_MAX};

        /* nullptr => 无穷小 */
        [[nodiscard]] bool less(const T &key, const Node *x) const {
            return x == nullptr || cmp(key, x->key.value());
        }

        [[nodiscard]] bool greater(const T &key, const Node *x) const {
            return x && cmp(x->key.value(), key);
        }

        [[nodiscard]] bool equal(const T &key, const Node *x) const {
            return x && key == x->key.value();
        }

        [[nodiscard]] int randHeight(int max_h = MAX_HEIGHT) {
            const auto now = std::chrono::system_clock::now();
            const unsigned long timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch()).count();
            std::mt19937_64 gen{timestamp};

            auto rand_x = dis(gen);
            return std::min(max_h, __builtin_popcount(rand_x));
        }

        [[nodiscard]] uint16_t getMaxHeight() const { return head.load(std::memory_order_relaxed)->height; }

        /* 寻找 >= key的节点 */
        /* 返回节点的前驱prev和本身 */
        template<typename NodeArr>
        int8_t findKeyNode(
                const T &key,
                Node *start,
                const uint8_t startLevel,
                NodeArr &preds,
                NodeArr &successor) {
            Node *prevNode = start;
            int8_t foundLevel = -1;
            Node *foundNode = nullptr;
            for (int level = startLevel; level >= 0; level--) {
                Node *next = prevNode->skip(level);
                while (greater(key, next)) {
                    prevNode = next;
                    next = next->skip(level);
                }

                if (foundLevel == -1 && !less(key, next)) {
                    foundLevel = level;
                    foundNode = next;
                }

                preds[level] = prevNode;
                successor[level] = foundNode ? foundNode : next;
            }

            return foundLevel;
        }

        template<typename NodeArr>
        int8_t findKeyNode(const T &key, NodeArr &preds, NodeArr &successor) {
            return findKeyNode(
                    key,
                    head.load(std::memory_order_acquire),
                    getMaxHeight() - 1,
                    preds,
                    successor);
        }

        bool lockNodesForChange(
                const int nodeHeight,
                std::unique_lock<spin_lock_type> guards[MAX_HEIGHT],
                std::array<Node *, MAX_HEIGHT> &preds,
                std::array<Node *, MAX_HEIGHT> &successor,
                const bool adding = true) {
            Node *pred, *self, *prevPred = nullptr;

            bool valid = true;
            for (int level = 0; valid && level < nodeHeight; level++) {
                pred = preds[level];
                DASSERT(pred != nullptr);

                self = successor[level];
                if (pred != prevPred) {
                    guards[level] = std::move(pred->acquireLock());
                    prevPred = pred;
                }

                /* double check */
                valid = !pred->markedForRemoval() && pred->skip(level) == self;
                if (adding)
                    valid = valid && (self == nullptr || !self->markedForRemoval());
            }

            return valid;
        }

        void growHeight(uint8_t h) {
            Node *oldHead = head.load(std::memory_order_acquire);
            /* 其它线程调用了grow height */
            if (oldHead->height >= h)
                return;

            Node *newHead = recycler.allocate(h);
            newHead->setIsHeadNode();
            /* 代码块，方便使用unique_lock */
            {
                std::unique_lock<spin_lock_type> lock = oldHead->acquireLock();
                newHead->copyHead(oldHead);
                Node *expected = oldHead;
                /* 如果其它线程已经更新head */
                if (!head.compare_exchange_strong(
                        expected, newHead, std::memory_order_release)) {
                    recycler.free(newHead);
                    return;
                }

                oldHead->setMarkedForRemoval();
            }

            recycler.free(oldHead);
        }

        static bool readyToDelete(Node *node, int level) {
            DASSERT(node != nullptr);
            return node->fullyLinked() && node->height - 1 == level && !node->markedForRemoval();
        }

        Node *findNode(const T &key) const {
            Node *pred = head.load(std::memory_order_acquire);
            int cur_h = pred->height;
            Node *cur = nullptr;

            bool found{false};
            while (!found) {
                while (cur_h > 0 && less(key, cur = pred->skip(cur_h - 1)))
                    cur_h--;

                if (cur_h == 0)
                    return nullptr;

                /* 后退一层 */
                cur_h--;

                while (greater(key, cur)) {
                    pred = cur;
                    cur = cur->skip(cur_h);
                }

                /* 循环结束，key > cur->key == false */
                /* 如果 key < cur->key == false */
                /* key 只能 == cur->key */
                found = !less(key, cur);
            }

            return cur;
        }

    public:
        SkipListLockFree() {
            head = recycler.allocate(1);
            head.load(std::memory_order_acquire)->setIsHeadNode();
        }

        ~SkipListLockFree() {
            for (Node *cur = head.load(std::memory_order_relaxed); cur != nullptr;) {
                Node *next = cur->skip(0);
                recycler.free(cur);
                cur = next;
            }
        }

        template<typename U>
        void push(U k) {
            std::array<Node *, MAX_HEIGHT> preds;
            std::array<Node *, MAX_HEIGHT> successor;
            size_t newSize{};
            while (true) {
                int max_level = getMaxHeight() - 1;
                int level = findKeyNode(k, preds, successor);

                /* key已经存在，或者其它线程正在添加 */
                if (level >= 0) {
                    Node *found = successor[level];
                    DASSERT(found != nullptr);
                    if (found->markedForRemoval())
                        continue;

                    while (UNLIKELY(!found->fullyLinked())) {}
                    found->incRefCount(1);
                    m_size.fetch_add(1, std::memory_order_release);
                    return;
                }

                int nodeHeight = randHeight(max_level + 1);
                std::unique_lock<spin_lock_type> guards[MAX_HEIGHT];
                if (!lockNodesForChange(nodeHeight, guards, preds, successor))
                    continue; // 其它线程正在持有节点，放弃并等待

                Node *newNode = recycler.allocate(nodeHeight, std::forward<U>(k));
                newNode->incRefCount(1);
                for (int i = 0; i < nodeHeight; i++) {
                    newNode->setSkip(i, successor[i]);
                    preds[i]->setSkip(i, newNode);
                }

                newNode->setFullyLinked();
                newSize = ++m_size;
                break;
            }

            /* 高度增长 */
            const int cur_h = getMaxHeight();
            const size_t limit = sizeLimit[cur_h];
            if (cur_h < MAX_HEIGHT && newSize > limit)
                growHeight(cur_h + 1);

            DASSERT(newSize > 0);
        }

        void push(T &&key) { push<T &&>(std::move(key)); }

        void push(const T &key) { push<const T &>(key); }

        bool erase(const T &key) {
            Node *nodeToBeDel = nullptr;
            bool isMarked = false;
            int nodeHeight = 0;
            std::array<Node *, MAX_HEIGHT> preds;
            std::array<Node *, MAX_HEIGHT> successor;
            std::unique_lock<spin_lock_type> nodeGuard;

            while (true) {
                int level = findKeyNode(key, preds, successor);
                /* key不存在于集合中, 或者其它线程尚未添加完成 */
                if (!isMarked && (level < 0 || !readyToDelete(successor[level], level)))
                    return false;

                if (!isMarked) {
                    nodeToBeDel = successor[level];
                    nodeHeight = nodeToBeDel->height;
                    nodeGuard = std::move(nodeToBeDel->acquireLock());
                    if (nodeToBeDel->markedForRemoval())
                        return false;

                    /* 引用计数器 > 0 */
                    if (nodeToBeDel->decRefCount(1) > 0) {
                        m_size.fetch_sub(1, std::memory_order_acquire);
                        return true;
                    }

                    nodeToBeDel->setMarkedForRemoval();
                    isMarked = true;
                }

                std::unique_lock<spin_lock_type> guards[MAX_HEIGHT];
                /* 获取节点控制权 */
                if (!lockNodesForChange(nodeHeight, guards, preds, successor, false))
                    continue;

                for (int i = nodeHeight - 1; i >= 0; i--)
                    preds[i]->setSkip(i, nodeToBeDel->skip(i));

                break;
            }

            m_size.fetch_sub(1, std::memory_order_acquire);
            recycler.free(nodeToBeDel);
            return true;
        }

        bool exists(const T &key) const { return findNode(key) != nullptr; }

        T front() const {
            while (true) {
                DASSERT(m_size.load(std::memory_order_acquire) > 0);
                Node *cur = head.load(std::memory_order_acquire);
                if (cur->m_lock.try_lock() == false)
                    continue;
                else if (cur->markedForRemoval()) {
                    cur->m_lock.unlock();
                    continue;
                }

                Node *next = cur->skip(0);
                T ans = next->key.value();
                cur->m_lock.unlock();
                return ans;
            }
        }

        [[nodiscard]] int size() const { return m_size.load(std::memory_order_acquire); }

        [[nodiscard]] bool empty() const { return size() == 0; }

        T pickup() {
            DASSERT(m_size > 0);
            T ans = front();
            erase(ans);
            return std::move(ans);
        }
    };
}
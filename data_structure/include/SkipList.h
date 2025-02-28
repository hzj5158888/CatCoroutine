#pragma once

#include <vector>
#include <algorithm>
#include <memory>
#include <random>
#include <chrono>

#include "inplace_vector.h"

namespace co {
    template<typename T, typename F = std::less<T>, typename Allocator = std::allocator<uint8_t>>
    class SkipList {
    private:
        constexpr static auto EXPECT_MAX_LEVEL = 32;

        enum FindOptions {
            NULL_OPT = 0,
            RECORD = 1,
            ERASE = 2,
            EXISTS = 4
        };

        struct Container {
            T data;
            uint32_t count{};

            explicit Container(T &&t) : data(std::move(t)) {}

            explicit Container(const T &t) : data(t) {}
        };

        struct Node {
            Container *container{};
            Node *next{};
            Node *down{};

            const T &get() const { return container->data; }
        };

        F cmp{};

        Node *head{};
        Node *under_level_head{};
        std::size_t m_size{};

        Allocator alloc{};

        std::uniform_int_distribution<> dis{0, 1};

        template<typename PathArr>
        void find(PathArr &path, const T &t, uint8_t option) {
            Node *cur = head;
            while (cur) {
                while (cur->next && !cmp(t, cur->next->get()) && t != cur->next->get())
                    cur = cur->next;

                if (option & RECORD)
                    path.push_back(cur);
                if (cur->next && t == cur->next->get() && option & (ERASE | EXISTS)) {
                    path.push_back(cur);
                    if (option & EXISTS)
                        return;
                }

                if (!cur->next || cmp(t, cur->next->get()) || t == cur->next->get())
                    cur = cur->down;
                else
                    break;
            }
        }

        void destroyContainer(Container *ptr) {
            ptr->~Container();
            alloc.deallocate(reinterpret_cast<uint8_t *>(ptr), sizeof(Container));
        }

        void destroyNode(Node *ptr) {
            ptr->~Node();
            alloc.deallocate(reinterpret_cast<uint8_t *>(ptr), sizeof(Node));
        }

        template<typename ... Args>
        Node *createNode(Args &&... args) {
            Node *ans = reinterpret_cast<Node *>(alloc.allocate(sizeof(Node)));
            new(ans) Node(std::forward<Args>(args)...);
            return ans;
        }

        template<typename ... Args>
        Container *createContainer(Args &&... args) {
            auto *ans = reinterpret_cast<Container *>(alloc.allocate(sizeof(Container)));
            new(ans) Container(std::forward<Args>(args)...);
            return ans;
        }

        [[nodiscard]] int RandLevel() {
            const auto now = std::chrono::system_clock::now();
            const unsigned long timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch()).count();
            std::mt19937_64 gen{timestamp};

            for (int i = 1;; i++) {
                if ((dis(gen) & 1) == 1)
                    return i;
            }
        }

    public:
        SkipList() {
            head = createNode();
            under_level_head = head;
        }

        ~SkipList() = default;

        template<typename V>
        void push(V t)
        {
            inplace_vector<Node *, EXPECT_MAX_LEVEL> path;
            find(path, t, RECORD);
            /* 集合中存在元素t */
            if (path.size() > 0 && path.back()->next && t == path.back()->next->get()) {
                path.back()->next->container->count++;
                m_size++;
                return;
            }

            Container *ptr = createContainer(std::forward<V>(t));
            ptr->count = 1;

            Node *prev_insert = nullptr;
            int insert_level = RandLevel();
            while (path.size() && insert_level > 0) {
                auto cur_level = path.back();
                path.pop_back();

                Node *insert = createNode();
                insert->container = ptr;
                insert->down = prev_insert;
                prev_insert = insert;

                insert->next = cur_level->next;
                cur_level->next = insert;

                insert_level--;
            }

            if (insert_level > 0) {
                Node *new_head = createNode();
                new_head->down = head;
                head = new_head;

                Node *insert = createNode();
                insert->container = ptr;
                insert->down = prev_insert;

                head->next = insert;
            }

            m_size++;
        }

        void push(const T &t) { push<const T &>(std::forward<const T &>(t)); }

        void push(T &&t) { push<T &&>(std::move(t)); }

        bool erase(const T &t)
        {
            if (m_size == 0) [[unlikely]]
                return false;

            inplace_vector<Node *, EXPECT_MAX_LEVEL> path;
            find(path, t, ERASE);
            if (path.size() == 0)
                return false;

            auto back_node = path.back()->next;
            if (back_node == nullptr || back_node->get() != t) [[unlikely]]
                return false;
            /* 等待最后回收 */
            auto container = back_node->container;
            container->count--;
            if (container->count > 0)
            {
                m_size--;
                return true;
            }

            while (path.size())
            {
                auto cur_node = path.back();
                path.pop_back();

                if (cur_node->next == nullptr) [[unlikely]]
                    break;

                auto del = cur_node->next;
                cur_node->next = cur_node->next->next;
                destroyNode(del);
            }

            m_size--;
            destroyContainer(container);
            return true;
        }

        bool exists(const T &t)
        {
            inplace_vector<Node *, 1> res;
            find(res, t, EXISTS);
            return res.size() > 0;
        }

        void clear()
        {
            while (true)
            {
                Node *cur = head;
                if (cur == under_level_head)
                    break;

                head = head->down;
                while (cur) {
                    auto next = cur->next;
                    destroyNode(cur);
                    cur = next;
                }
            }

            /* 跳过头结点 */
            Node *cur = head->next;
            while (cur) {
                auto next = cur->next;
                destroyContainer(next->container);
                destroyNode(next);
                cur = next;
            }
        }

        const T &front() const { return under_level_head->next->get(); }

        [[nodiscard]] int32_t size() const { return m_size; }
    };
}

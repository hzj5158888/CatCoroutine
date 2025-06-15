#pragma once

#include <functional>
#include <map>
#include <sys/epoll.h>

#include "../../utils/include/utils.h"
#include "../../utils/include/spin_lock_sleep.h"
#include "../../allocator/include/MemoryPool.h"

namespace co {
    class Epoller
    {
    public:
        using spin_lock_t = spin_lock_sleep;
        using req_callback_t = std::function<void(const struct epoll_event &)>;

        struct Request
        {
            int fd{-1};
            struct epoll_event * event{};
            req_callback_t callback{};
            MemoryPool * pool{};

            Request() = delete;

            explicit Request(MemoryPool * p)
            {
                if (UNLIKELY(!p))
                    throw std::runtime_error("request constructor: null MemoryPool");

                pool = p;
                event = static_cast<struct epoll_event *>(p->allocate(sizeof(struct epoll_event)));
            }

            Request(Request && oth) noexcept { swap(std::move(oth)); }

            ~Request()
            {
                if (pool)
                    pool->deallocate(event);
            }

            void swap(Request && oth) noexcept
            {
                std::swap(fd, oth.fd);
                std::swap(event, oth.event);
                std::swap(callback, oth.callback);
                std::swap(pool, oth.pool);
            }
        };

        int m_ep_fd{-1};
        std::map<int, Request> m_requests{};
        spin_lock_t m_lock{};
        MemoryPool m_pool{};

        Epoller();
        ~Epoller();

        void waiter();
        bool add(int fd, struct epoll_event * event, const req_callback_t & cb);
        bool cancel(int fd);
    };
}
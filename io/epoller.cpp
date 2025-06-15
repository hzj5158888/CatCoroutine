#include <cerrno>
#include <sys/epoll.h>

#include "./include/epoller.h"

namespace co {
    Epoller::Epoller()
    {
        m_ep_fd = epoll_create1(0);
        if (m_ep_fd == -1)
            throw std::runtime_error("epoller create failed, errno = " + std::to_string(errno));
    }

    Epoller::~Epoller()
    {
        if (m_ep_fd != -1)
            close(m_ep_fd);
    }

    bool Epoller::cancel(int fd)
    {
        if (fd == -1)
            throw std::invalid_argument("Epoller::cancel: invalid fd");

        int res = epoll_ctl(m_ep_fd, EPOLL_CTL_DEL, fd, nullptr);
        if (res != 0)
            return false;

        std::lock_guard lock(m_lock);
        m_requests.erase(fd);
        return true;
    }

    bool Epoller::add(int fd, struct epoll_event * event, const req_callback_t & cb)
    {
        if (fd == -1)
            throw std::invalid_argument("Epoller::add: invalid fd");

        Request request{&m_pool};
        request.fd = fd;
        request.callback = cb;
        if (event)
            std::memcpy(request.event, event, sizeof(struct epoll_event));

        std::lock_guard lock(m_lock);
        int res = epoll_ctl(m_ep_fd, EPOLL_CTL_ADD, fd, request.event);
        if (res == -1)
            return false;

        m_requests.insert({fd, std::move(request)});
        return true;
    }

    void Epoller::waiter()
    {
        if (m_ep_fd == -1)
            throw std::runtime_error("Epoller::waiter: un init");

        constexpr int events_size = 128;
        struct epoll_event events[events_size]{};
        while (true)
        {
            int cur_size = epoll_wait(m_ep_fd, events, events_size, -1);
            std::lock_guard lock(m_lock);
            for (int i = 0; i < cur_size; i++)
            {
                auto cur_req = m_requests.find(events[i].data.fd);
                if (UNLIKELY(cur_req == m_requests.end()))
                    throw std::runtime_error("Epoller::waiter: invalid fd = " + std::to_string(events[i].data.fd));

                cur_req->second.callback(events[i]);
                m_requests.erase(cur_req);
            }
        }
    }
}
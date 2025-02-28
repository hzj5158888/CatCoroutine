//
// Created by hzj on 25-1-12.
//

#pragma once

#include <cstdint>
#include <utility>
#include <exception>
#include <atomic>

#include "../../include/CoCtx.h"
#include "../../include/CoPrivate.h"
#include "../../sched/include/Scheduler.h"
#include "../../data_structure/include/RingBufferLockFree.h"
#include "Semaphore.h"
#include "utils.h"
#include "RingBuffer.h"

namespace co {
	class ChannelClosedException : public std::exception
	{
	public:
		[[nodiscard]] const char * what() const noexcept override
		{
			return "ChannelClosedException";
		}
	};

	template<typename T, std::size_t SIZE = 1>
	class Channel
	{
	private:
        //RingBufferLockFree<T> buffer{SIZE};
        RingBuffer<T, SIZE> buffer{};
        spin_lock_sleep m_lock{};
        std::atomic<bool> is_close{false};
		Semaphore sender{}, receiver{SIZE};
        Semaphore full{}, empty{SIZE};
	public:
        Channel() = default;
        Channel(const Channel & chan) = delete;
        Channel(Channel && chan) = delete;
		~Channel() { close(); }

		template<class V>
		void push(V && x)
		{
			if (UNLIKELY(is_close.load(std::memory_order_relaxed)))
				throw ChannelClosedException();

#ifdef __STACK_DYN__
            bool is_exec{};
			receiver.wait_then([&x, &is_exec](Co_t * receiver_co)
            {
                *(reinterpret_cast<T*>(receiver_co->chan_receiver)) = std::forward<V>(x);
                receiver_co->chan_has_value = true;
                is_exec = true;
            });
            if (!is_exec)
            {
                empty.wait();
                m_lock.lock();
                assert(buffer.push(std::forward<V>(x)));
                m_lock.unlock();
                full.signal();
            }
#else
            receiver.wait();
            buffer.push(std::forward<V>(x));
#endif
			sender.signal();
		}

        template<class ... Args>
        void emplace(Args ... args)
        {
            if (UNLIKELY(is_close.load(std::memory_order_relaxed)))
                throw ChannelClosedException();

#ifdef __STACK_DYN__
            bool is_exec{};
            receiver.wait_then([&is_exec, &args...](Co_t * wakeup_co)
            {
                auto obj_ptr = reinterpret_cast<T*>(wakeup_co->chan_receiver);
                *obj_ptr = T{std::forward<Args>(args)...};
                wakeup_co->chan_has_value = true;
                is_exec = true;
            });
            if (!is_exec)
            {
                empty.wait();
                m_lock.lock();
                assert(buffer.emplace(std::forward<Args>(args)...));
                m_lock.unlock();
                full.signal();
            }
#else
            receiver.wait();
            assert(buffer.emplace(std::forward<Args>(args)...));
#endif
            sender.signal();
        }

		void pull(T & ans)
		{
			if (UNLIKELY(is_close.load(std::memory_order_relaxed) && sender.count() == 0))
				throw ChannelClosedException();

#ifdef __STACK_DYN__
            auto cur_co = co_ctx::loc->scheduler->running_co;
            cur_co->chan_receiver = std::addressof(ans);
            cur_co->chan_has_value = false;

			sender.wait();
            receiver.signal();
            if (!cur_co->chan_has_value)
            {
                full.wait();
                m_lock.lock();
                assert(buffer.pop(ans));
                m_lock.unlock();
                empty.signal();
            }
#else
            sender.wait();
            ans = buffer.pop(ans);
            receiver.signal();
#endif
		}

		inline void close() { is_close = true; }
        [[nodiscard]] inline int32_t size() const { return static_cast<int64_t>(sender); }

        Channel & operator >> (T & x)
        {
            pull(x);
            return *this;
        }

        template<class V>
        Channel & operator << (V && x)
        {
            push(std::forward<V&&>(x));
            return *this;
        }
	};
}

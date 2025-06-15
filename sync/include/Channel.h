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
#include "../../data_structure/include/RingBufferLock.h"
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
        RingBufferLock<T, SIZE> buffer{};
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
            auto callback = [&x, &is_exec](Co_t * receiver_co)
            {
                *(reinterpret_cast<T*>(receiver_co->recv_buffer)) = std::forward<V>(x);
                receiver_co->buffer_has_value = true;
                is_exec = true;
            };
			receiver.wait_then([&callback](Co_t * receiver_co) { callback(receiver_co); });
            if (!is_exec)
            {
                empty.wait();
                assert(buffer.push(std::forward<V>(x)));
                full.signal();
            }
#else
            receiver.wait();
            buffer.push(std::forward<V>(x));
#endif
			sender.signal();
		}

        template<class V>
        bool push_for(V && x, std::chrono::microseconds duration)
        {
            if (UNLIKELY(is_close.load(std::memory_order_relaxed)))
                throw ChannelClosedException();

#ifdef __STACK_DYN__
            auto stage_1 = std::chrono::microseconds(co_ctx::clock.rdus());

            bool is_exec{};
            auto callback = [&x, &is_exec](Co_t * receiver_co)
            {
                *(reinterpret_cast<T*>(receiver_co->recv_buffer)) = std::forward<V>(x);
                receiver_co->buffer_has_value = true;
                is_exec = true;
            };
            bool timeout = receiver.wait_for_then(duration, [&callback](Co_t * receiver_co)
            {
                callback(receiver_co);
            });
            if (timeout)
                return false;

            auto stage_2 = std::chrono::microseconds(co_ctx::clock.rdus());
            auto stage_1_cost = stage_2 - stage_1;
            if (!is_exec)
            {
                auto success = empty.wait_for(duration - stage_1_cost);
                if (!success)
                    return false;

                assert(buffer.push(std::forward<V>(x)));
                full.signal();
            }
#else
            receiver.wait();
            buffer.push(std::forward<V>(x));
#endif
            sender.signal();
            return true;
        }

        template<class ... Args>
        void emplace(Args ... args)
        {
            if (UNLIKELY(is_close.load(std::memory_order_relaxed)))
                throw ChannelClosedException();

#ifdef __STACK_DYN__
            bool is_exec{};
            auto callback = [&is_exec, &args...](Co_t * wakeup_co)
            {
                auto obj_ptr = reinterpret_cast<T*>(wakeup_co->recv_buffer);
                *obj_ptr = T{std::forward<Args>(args)...};
                wakeup_co->buffer_has_value = true;
                is_exec = true;
            };
            receiver.wait_then([&callback](Co_t * wakeup_co) { callback(wakeup_co); });
            if (!is_exec)
            {
                empty.wait();
                assert(buffer.emplace(std::forward<Args>(args)...));
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
            cur_co->recv_buffer = std::addressof(ans);
            cur_co->buffer_has_value = false;

			sender.wait();
            receiver.signal();
            if (!cur_co->buffer_has_value)
            {
                full.wait();
                assert(buffer.pop(ans));
                empty.signal();
            }
#else
            sender.wait();
            ans = buffer.pop(ans);
            receiver.signal();
#endif
		}

        bool pull_for(T & ans, std::chrono::microseconds duration)
        {
            if (UNLIKELY(is_close.load(std::memory_order_relaxed) && sender.count() == 0))
                throw ChannelClosedException();

#ifdef __STACK_DYN__
            auto cur_co = co_ctx::loc->scheduler->running_co;
            cur_co->recv_buffer = std::addressof(ans);
            cur_co->buffer_has_value = false;

            auto stage_1 = std::chrono::microseconds(co_ctx::clock.rdus());
            bool timeout = sender.wait_for(duration);
            if (timeout)
                return false;

            receiver.signal();

            auto stage_2 = std::chrono::microseconds(co_ctx::clock.rdus());
            auto stage_1_cost = stage_2 - stage_1;
            if (!cur_co->buffer_has_value)
            {
                auto success = full.wait_for(duration - stage_1_cost);
                if (!success)
                    return false;

                assert(buffer.pop(ans));
                empty.signal();
            }
#else
            sender.wait_for(duration);
            ans = buffer.pop(ans);
            receiver.signal();
#endif
            return true;
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

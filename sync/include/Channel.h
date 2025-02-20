//
// Created by hzj on 25-1-12.
//

#ifndef COROUTINE_UTILS_INCLUDE_CHANNEL_H
#define COROUTINE_UTILS_INCLUDE_CHANNEL_H

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/queue.hpp>
#include <cstdint>
#include <utility>
#include <exception>
#include <atomic>

#include "Semaphore.h"

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
		boost::lockfree::queue<T, boost::lockfree::capacity<SIZE>> buffer{};
		std::atomic<bool> is_close{false};
		Semaphore sem_full{}, sem_empty{SIZE};

	public:
        Channel() = default;
        Channel(const Channel & chan) = delete;
        Channel(Channel && chan) = delete;
		~Channel() { close(); }

		template<class V>
		void push(V x)
		{
			if (UNLIKELY(is_close.load(std::memory_order_relaxed)))
				throw ChannelClosedException();

			sem_empty.wait();
			buffer.push(std::forward<V>(x));
			sem_full.signal();
		}

		void pull(T & ans)
		{
			if (UNLIKELY(is_close.load(std::memory_order_relaxed) && (int64_t)sem_full == 0))
				throw ChannelClosedException();

			sem_full.wait();
			buffer.pop(ans);
			sem_empty.signal();
		}

		inline void close() { is_close = true; }
        [[nodiscard]] inline int32_t size() const { return static_cast<int64_t>(sem_full); }
	};
}

#endif //COROUTINE_UTILS_INCLUDE_CHANNEL_H

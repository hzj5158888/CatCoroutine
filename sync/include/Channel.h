//
// Created by hzj on 25-1-12.
//

#ifndef COROUTINE_UTILS_INCLUDE_CHANNEL_H
#define COROUTINE_UTILS_INCLUDE_CHANNEL_H

#include <utility>
#include <exception>
#include <string>
#include <atomic>

#include "Semaphore.h"
#include "Mutex.h"
#include "../../utils/include/spin_lock.h"
#include "../../data_structure/include/RingBuffer.h"

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
		RingBuffer<T, SIZE> buffer{};
		std::atomic<bool> is_close{false};
		Semaphore sem_full{}, sem_empty{SIZE};
        spin_lock m_lock{};

		template<class V>
		void push(V x)
		{
			if (UNLIKELY(is_close.load(std::memory_order_acquire)))
				throw ChannelClosedException();

			sem_empty.wait();
			m_lock.lock();
			buffer.push(std::forward<V>(x));
			m_lock.unlock();
			sem_full.signal();
		}
	public:
        Channel() = default;
        Channel(const Channel & chan) = delete;
        Channel(Channel && chan) = delete;
		inline ~Channel() { close(); }

		inline void push(const T & x) { push<const T &>(std::forward<const T &>(x)); }
		inline void push(T && x) { push<T &&>(std::move(x)); }

		inline T pull()
		{
			if (UNLIKELY(is_close && (int64_t)sem_full == 0))
				throw ChannelClosedException();

			sem_full.wait();
			m_lock.lock();
			auto ans = std::move(buffer.pop());
			m_lock.unlock();
			sem_empty.signal();
			return ans;
		}

		inline void close() { is_close = true; }
        [[nodiscard]] inline int32_t size() const { return buffer.size(); }
	};
}

#endif //COROUTINE_UTILS_INCLUDE_CHANNEL_H

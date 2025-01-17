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
		const char * what() const throw ()
		{
			return "ChannelClosedException";
		}
	};

	template<typename T, std::size_t size>
	class Channel
	{
	private:
		RingBuffer<T, size> buffer{};
		std::atomic<bool> is_close{false};
		Semaphore sem_full{}, sem_empty{size};
		Mutex m_lock{};

		template<class V>
		void push(V x)
		{
			if (is_close) [[unlikely]]
				throw ChannelClosedException();

			sem_empty.wait();
			m_lock.lock();
			buffer.push(std::forward<V>(x));
			m_lock.unlock();
			sem_full.signal();
		}
	public:
		inline Channel() = default;
		inline Channel(const Channel & chan) = delete;
		inline Channel(Channel && chan) = delete;
		inline ~Channel() { close(); }

		inline void push(const T & x) { push<const T &>(std::forward<const T &>(x)); }
		inline void push(T && x) { push<T &&>(std::move(x)); }

		inline T && pull()
		{
			if (is_close && sem_full == 0) [[unlikely]]
				throw ChannelClosedException();

			sem_full.wait();
			m_lock.lock();
			auto ans = std::move(buffer.pop());
			m_lock.unlock();
			sem_empty.signal();

			return std::move(ans);
		}

		inline void close() { is_close = true; }
	};
}

#endif //COROUTINE_UTILS_INCLUDE_CHANNEL_H

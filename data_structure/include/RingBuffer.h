//
// Created by hzj on 25-1-10.
//

#ifndef COROUTINE_RINGBUFFER_H
#define COROUTINE_RINGBUFFER_H

#include <utility>
#include <array>
#include <cassert>

template<typename T, std::size_t N>
class RingBuffer
{
private:

    std::size_t m_size{};
    std::size_t rear{}, front{};
    std::array<T, N> m_data;

    static_assert(N > 0);
public:
    RingBuffer() = default;
	RingBuffer(RingBuffer && buf) noexcept { swap(std::move(buf)); }

	void swap(RingBuffer && buf)
	{
		std::swap(m_data, buf.m_data);
		std::swap(rear, buf.rear);
		std::swap(front, buf.front);
		std::swap(m_size, buf.m_size);
	}

    [[nodiscard]] bool empty() const
    {
        return m_size == 0;
    }

    [[nodiscard]] std::size_t size() const
    {
        return m_size;
    }

    [[nodiscard]] bool full() const
    {
        return m_size == N;
    }

    bool push(const T & x)
    {
        if (full())
            return false;

        m_data[rear++] = x;
        rear %= N;
        m_size++;
        return true;
    }

    bool push(T && x)
    {
        if (full())
            return false;

        m_data[rear++] = std::move(x);
        rear %= N;
        m_size++;
        return true;
    }

    T pop()
    {
        auto tmp = front++;
        front %= N;
        m_size--;
        return m_data[tmp];
    }
};

template<typename T>
class RingBufferDyn
{
private:
	std::size_t N;
	std::size_t m_size{};
	std::size_t rear{}, front{};
	std::vector<T> m_data;
public:
	explicit RingBufferDyn(std::size_t n) : N(n) { assert(n > 0); m_data.resize(n); };

	[[nodiscard]] bool empty() const
	{
		return m_size == 0;
	}

	[[nodiscard]] std::size_t size() const
	{
		return m_size;
	}

	[[nodiscard]] bool full() const
	{
		return m_size == N;
	}

	bool push(const T & x)
	{
		if (full())
			return false;

		m_data[rear++] = x;
		rear %= N;
		m_size++;
		return true;
	}

	bool push(T && x)
	{
		if (full())
			return false;

		m_data[rear++] = std::move(x);
		rear %= N;
		m_size++;
		return true;
	}

	T && pop()
	{
		assert(!empty());
		auto tmp = front++;
		front %= N;
		return std::move(m_data[tmp]);
	}
};


#endif //COROUTINE_RINGBUFFER_H

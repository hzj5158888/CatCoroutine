//
// Created by hzj on 25-1-10.
//

#pragma once

#include <utility>
#include <array>
#include <cassert>
#include <vector>
#include <atomic>

template<typename T, size_t N>
class RingBufferSafe
{
private:

    std::atomic<size_t> m_size{};
    std::atomic<size_t> rear{}, front{};
    std::array<T, N> m_data;

    static_assert(N > 0);
public:
    RingBufferSafe() = default;
	RingBufferSafe(RingBufferSafe && buf) noexcept { swap(std::move(buf)); }

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

    T && pop()
    {
        assert(!empty());
        auto tmp = front++;
        front %= N;
        return std::move(m_data[tmp]);
    }
};

template<typename T>
class RingBufferDynSafe
{
private:
	std::size_t N;
	std::size_t m_size{};
	std::size_t rear{}, front{};
	std::vector<T> m_data;
public:
	explicit RingBufferDynSafe(std::size_t n) : N(n) { assert(n > 0); m_data.resize(n); };

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

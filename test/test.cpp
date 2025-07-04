//
// Created by hzj on 25-1-16.
//

#include <iostream>
#include <string>
#include <atomic>
#include <chrono>

#include "include/test.h"
#include "../include/Coroutine.h"
#include "../sync/include/Semaphore.h"
#include "../sync/include/Channel.h"

std::chrono::nanoseconds start_time;

void end_of_test()
{
	std::cout << std::endl;
	std::cout << std::endl;
}

void start_cal()
{
	start_time = std::chrono::system_clock::now().time_since_epoch();
}

void end_cal()
{
	auto cost = std::chrono::system_clock::now().time_since_epoch() - start_time;
	std::cout << "time cost: " << std::chrono::duration_cast<std::chrono::milliseconds>(cost).count() << "ms" << std::endl;
}

template<typename Fn, typename ... Args, typename Ret = std::invoke_result_t<Fn, Args...>>
Ret benchmark(Fn && fn, Args &&... args)
{
    std::cout << "start benchmark" << std::endl;
    auto start = std::chrono::system_clock::now().time_since_epoch();
    if constexpr (std::is_same_v<Ret, void>)
    {
        fn(std::forward<Args>(args)...);
        auto cost = std::chrono::system_clock::now().time_since_epoch() - start;
        std::cout << "time cost: " << std::chrono::duration_cast<std::chrono::milliseconds>(cost).count() << "ms" << std::endl;
    } else {
        auto res = fn(std::forward<Args>(args)...);
        auto cost = std::chrono::system_clock::now().time_since_epoch() - start;
        std::cout << "time cost: " << std::chrono::duration_cast<std::chrono::milliseconds>(cost).count() << "ms" << std::endl;
        return res;
    }
}

void basic_test()
{
	std::cout << "basic test" << std::endl;

	auto work_loop = [] (const std::string & s, std::atomic<int> & g_count)
	{
		constexpr auto print_round = 50;
		for (int i = 0; i < print_round; i++)
		{
			std::cout << s << g_count;
			std::cout << "\n";
			std::cout << std::flush;

			g_count++;
			co::yield();
		}
	};

	std::atomic<int> g_count{};
	auto c1 = co::Co<void>{work_loop, "X", std::ref(g_count)};
	auto c2 = co::Co<void>{work_loop, "Y", std::ref(g_count)};
	c1.await();
	c2.await();

	std::cout << "basic test end" << std::endl;
	end_of_test();
}

void single_switch_test()
{
    auto work_loop = [] (std::atomic<int> & g_count)
    {
        constexpr auto switch_round = 1000000000;
        for (int i = 0; i < switch_round; i++)
        {
            //std::cout << g_count;
            //std::cout << "\n";

            g_count.fetch_add(1, std::memory_order_relaxed);
            co::yield();
        }
    };

    std::cout << "single coroutine switch test" << std::endl;
    start_cal();

    std::atomic<int> g_count{};
    co::Co<void>{work_loop, std::ref(g_count)}.await();
    std::cout << "single coroutine switch_count = " << g_count << std::endl;

    end_cal();
    end_of_test();
}

void multiple_switch_test()
{
	//std::ios::sync_with_stdio(false);
	auto work_loop = [] (std::atomic<int> & g_count)
	{
		constexpr auto print_round = 2000;
		for (int i = 0; i < print_round; i++)
		{
			//std::cout << g_count;
			//std::cout << "\n";

			g_count.fetch_add(1, std::memory_order_relaxed);
			co::yield();
		}
	};

	struct co_pair
	{
		co::Co<void> x, y;
	};

	std::cout << "multiple coroutine switch test" << std::endl;

    constexpr auto coroutine_cnt = 500000;
    std::atomic<int> g_count{};
    std::vector<co_pair> vec{};
	vec.reserve(coroutine_cnt / 2);
	start_cal();
	for (int i = 0; i < coroutine_cnt / 2; i++)
	{
		//auto c1 = co::Co{work_loop, "<X, " + std::to_string(i) + ">: ", std::ref(g_count)};
		//auto c2 = co::Co{work_loop, "<Y, " + std::to_string(i) + ">: ", std::ref(g_count)};
		auto c1 = co::Co<void>{work_loop, std::ref(g_count)};
		auto c2 = co::Co<void>{work_loop, std::ref(g_count)};
		vec.push_back({std::move(c1), std::move(c2)});
	}

	for (auto & [c1, c2] : vec)
	{
		c1.await();
		c2.await();
	}

	std::ios::sync_with_stdio(true);
	std::cout << "multiple multiple switch_count = " << g_count << std::endl;
	end_cal();
	end_of_test();
}

void semaphore_test()
{
    //std::ios::sync_with_stdio(false);

    auto work_loop = [] (co::Semaphore & x, co::Semaphore & y, int flag)
    {
        constexpr auto print_round = 2000;
        for (int i = 0; i < print_round; i++)
        {
            //std::cout << g_count;
            //std::cout << "\n";

            if (i & 1)
            {
                if (flag)
                    x.signal();
                else
                    y.wait();
            } else {
                if (flag)
                    y.signal();
                else
                    x.wait();
            }

            co::yield();
        }
    };

    struct co_pair
    {
        co::Co<void> x, y;
    };

    std::cout << "coroutine semaphore test" << std::endl;

    constexpr auto coroutine_cnt = 500000;
    co::Semaphore x{}, y{};
    std::vector<co_pair> vec{};
    vec.reserve(coroutine_cnt / 2);
    start_cal();
    for (int i = 0; i < coroutine_cnt / 2; i++)
    {
        //auto c1 = co::Co{work_loop, "<X, " + std::to_string(i) + ">: ", std::ref(g_count)};
        //auto c2 = co::Co{work_loop, "<Y, " + std::to_string(i) + ">: ", std::ref(g_count)};
        auto c1 = co::Co<void>{work_loop, std::ref(x), std::ref(y), true};
        auto c2 = co::Co<void>{work_loop, std::ref(x), std::ref(y), false};
        vec.push_back({std::move(c1), std::move(c2)});
    }

    for (auto & [c1, c2] : vec)
    {
        c1.await();
        c2.await();
    }

    std::ios::sync_with_stdio(true);
    std::cout << "coroutine semaphore count = " << static_cast<int64_t>(x) << ", " << static_cast<int64_t>(y) << std::endl;
    end_cal();
    end_of_test();
}

void channel_test()
{
    std::ios::sync_with_stdio(true);

    using chan_t = co::Channel<int, 128>;

    std::atomic<int> d_print_count{};
    std::array<std::atomic<int>, 2000> count;
    auto work_loop = [&count, &d_print_count] (chan_t & chan, int co_idx, int flag)
    {
        constexpr auto print_round = 2000;
        for (int i = 0; i < print_round; i++)
        {
            //std::cout << g_count;
            //std::cout << "\n";
            if (flag)
            {
                chan << i;
                count[i]++;
            } else {
                int res{};
                chan >> res;
                count[res]--;
            }
            co::yield();
        }

        if ((++d_print_count) % 40960 == 0)
        {
            std::string s = "coroutine end, " + std::to_string(co_idx);
            std::cout << s << std::endl;
        }
    };

    struct co_pair
    {
        co::Co<void> x, y;
    };

    std::cout << "coroutine channel test" << std::endl;

    constexpr auto coroutine_cnt = 500000;
    chan_t chan{};
    std::vector<co_pair> vec{};
    vec.reserve(coroutine_cnt / 2);
    start_cal();
    for (int i = 0; i < coroutine_cnt / 2; i++)
    {
        //auto c1 = co::Co{work_loop, "<X, " + std::to_string(i) + ">: ", std::ref(g_count)};
        //auto c2 = co::Co{work_loop, "<Y, " + std::to_string(i) + ">: ", std::ref(g_count)};
        auto c1 = co::Co<void>{work_loop, std::ref(chan), i, true};
        auto c2 = co::Co<void>{work_loop, std::ref(chan), coroutine_cnt / 2 + i, false};
        vec.emplace_back(std::move(c1), std::move(c2));
    }

    for (auto & [c1, c2] : vec)
    {
        c1.await();
        c2.await();
    }

    std::cout << "coroutine channel elem count = " << static_cast<int64_t>(chan.size()) << std::endl;
    end_cal();

    for (int i = 0; i < 2000; i++)
        assert(count[i] == 0 && "unclear count");

    end_of_test();
}

void sleep_test()
{
    std::cout << "coroutine sleep test" << std::endl;

    using namespace std::chrono;

    constexpr auto test_round = 100;
    std::atomic<int> d_print_count{};
    std::atomic<uint64_t> delta_sum{};
    auto func = [&delta_sum, &d_print_count](int co_id)
    {
        constexpr microseconds sleep_time = milliseconds(100);
        microseconds prev = microseconds(co::co_ctx::clock.rdus()) - sleep_time;
        for (int i = 0; i < test_round; i++)
        {
            microseconds now = microseconds(co::co_ctx::clock.rdus());

            //std::string str =
                   // "sleep, co_id = " + std::to_string(co_id)
                    //+ ", round = " + std::to_string(i)
                    //+ ", delta = " + std::to_string((now - prev).count());

            //std::cout << str << std::endl;
            delta_sum += (now - prev).count();
            prev = now;
            co::sleep(sleep_time);
        }

        if ((++d_print_count) % 4096 == 0)
        {
            std::string s = "coroutine end, " + std::to_string(co_id);
            std::cout << s << std::endl;
        }
    };

    constexpr auto coroutine_cnt = 500000;
    std::vector<co::Co<void>> vec{};
    for (int i = 0; i < coroutine_cnt; i++)
        vec.emplace_back(func, i);

    for (auto & v : vec)
        v.await();

    std::cout << "average delta time(us) = " << (delta_sum / (coroutine_cnt * test_round)) << std::endl;
    std::cout << "coroutine sleep test end" << std::endl;
}

void channel_timed_test()
{
    //std::ios::sync_with_stdio(false);

    using chan_t = co::Channel<int, 128>;

    std::atomic<int> d_print_count{};
    std::array<std::atomic<int>, 2000> count;
    auto work_loop = [&count, &d_print_count] (chan_t & chan, int co_idx, int flag)
    {
        constexpr auto print_round = 2000;
        uint64_t max_wait_time{25};
        uint64_t min_wait_time{5};
        for (int i = 0; i < print_round; i++)
        {
            //std::cout << g_count;
            //std::cout << "\n";
            auto cur_wait_time = std::max(min_wait_time, (co::co_ctx::loc->rand() % max_wait_time));
            if (flag)
            {
                auto timeout = chan.push_for(i, std::chrono::milliseconds(cur_wait_time));
                if (!timeout)
                    count[i]++;
            } else {
                int res{};
                auto timeout = chan.pull_for(res, std::chrono::milliseconds(cur_wait_time));
                if (!timeout)
                    count[res]--;
            }
            co::yield();
        }

        if ((++d_print_count) % 4096 == 0)
        {
            std::string s = "coroutine end, " + std::to_string(co_idx);
            std::cout << s << std::endl;
        }
    };

    struct co_pair
    {
        co::Co<void> x, y;
    };

    std::cout << "timed coroutine channel test" << std::endl;

    constexpr auto coroutine_cnt = 500000;
    chan_t chan{};
    std::vector<co_pair> vec{};
    vec.reserve(coroutine_cnt / 2);
    start_cal();
    for (int i = 0; i < coroutine_cnt / 2; i++)
    {
        //auto c1 = co::Co{work_loop, "<X, " + std::to_string(i) + ">: ", std::ref(g_count)};
        //auto c2 = co::Co{work_loop, "<Y, " + std::to_string(i) + ">: ", std::ref(g_count)};
        auto c1 = co::Co<void>{work_loop, std::ref(chan), i, true};
        auto c2 = co::Co<void>{work_loop, std::ref(chan), coroutine_cnt / 2 + i, false};
        vec.emplace_back(std::move(c1), std::move(c2));
    }

    for (auto & [c1, c2] : vec)
    {
        c1.await();
        c2.await();
    }

    std::ios::sync_with_stdio(true);
    std::cout << "coroutine channel elem count = " << static_cast<int64_t>(chan.size()) << std::endl;
    end_cal();

    for (int i = 0; i < 2000; i++)
        assert(count[i] == 0 && "unclear count");

    end_of_test();
}

int fib_await(int x)
{
    if (x <= 2)
        return x == 0 ? 0 : 1;

    co::Co<int> co_a{fib_await, x - 1};
    co::Co<int> co_b{fib_await, x - 2};
    return co_a.await() + co_b.await();
}

int fib_normal(int x)
{
    if (x <= 2)
        return x == 0 ? 0 : 1;

    return fib_normal(x - 1) + fib_normal(x - 2);
}

void test()
{
    std::cout << benchmark(fib_await, 30) << std::endl;
    std::cout << benchmark(fib_normal, 30) << std::endl;
	//basic_test();
    //single_switch_test();
    //multiple_switch_test();
    //semaphore_test();
    //channel_test();
    //sleep_test();
    //channel_timed_test();
}
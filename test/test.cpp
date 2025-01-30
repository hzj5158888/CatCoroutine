//
// Created by hzj on 25-1-16.
//

#include <iostream>
#include <string>
#include <atomic>
#include <chrono>

#include "include/test.h"
#include "../include/Coroutine.h"

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
	auto c1 = co::Co{work_loop, "X", std::ref(g_count)};
	auto c2 = co::Co{work_loop, "Y", std::ref(g_count)};
	c1.await();
	c2.await();

	std::cout << "basic test end" << std::endl;
	end_of_test();
}

void dyn_alloc_test()
{
	std::ios::sync_with_stdio(false);
	auto work_loop = [] (std::atomic<int> & g_count)
	{
		constexpr auto print_round = 100;
		for (int i = 0; i < print_round; i++)
		{
			/*
			std::cout << g_count;
			std::cout << "\n" << std::flush;
			*/

			g_count++;
			co::yield();
		}
	};

	struct coro
	{
		co::Co<co::PRIORITY_NORMAL> x, y;
	};

	constexpr auto coroutine_cnt = 4000;

	std::cout << "dyn_alloc_test" << std::endl;

	start_cal();

	std::atomic<int> g_count{};
	std::vector<coro> vec{};
	vec.reserve(coroutine_cnt / 2);
	for (int i = 0; i < coroutine_cnt / 2; i++)
	{
		//auto c1 = co::Co{work_loop, "<X, " + std::to_string(i) + ">: ", std::ref(g_count)};
		//auto c2 = co::Co{work_loop, "<Y, " + std::to_string(i) + ">: ", std::ref(g_count)};
		auto c1 = co::Co{work_loop, std::ref(g_count)};
		auto c2 = co::Co{work_loop, std::ref(g_count)};
		vec.push_back({std::move(c1), std::move(c2)});
	}

	for (auto & [c1, c2] : vec)
	{
		c1.await();
		c2.await();
	}

	std::ios::sync_with_stdio(true);
	std::cout << "dyn alloc test end" << std::endl;
	std::cout << "g_count = " << g_count << std::endl;
	end_cal();
	end_of_test();
}

void test()
{
	std::cout << "test 1" << std::endl;
	//basic_test();
	dyn_alloc_test();
}
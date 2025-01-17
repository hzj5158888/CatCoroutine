//
// Created by hzj on 25-1-16.
//

#include <iostream>
#include <string>
#include <atomic>

#include "include/test.h"
#include "../include/Coroutine.h"

void end_of_test()
{
	std::cout << std::endl;
	std::cout << std::endl;
}

void work_loop(const std::string & s, std::atomic<int> & g_count)
{
	for (int i = 0; i < 50; i++)
	{
		std::cout << s << g_count;
		printf("  ");

		g_count++;
		co::yield();
	}
}

void basic_test()
{
	std::cout << "basic test" << std::endl;

	std::atomic<int> g_count{};
	auto c1 = co::Co{work_loop, "X", std::ref(g_count)};
	auto c2 = co::Co{work_loop, "Y", std::ref(g_count)};
	c1.await();
	c2.await();

	end_of_test();
}

void dyn_alloc_test()
{
	std::cout << "dyn_alloc_test" << std::endl;

	struct coro
	{
		co::Co<co::PRIORITY_NORMAL> x, y;
	};

	std::atomic<int> g_count{};

	std::vector<coro> vec{};
	for (int i = 0; i < 8; i++)
	{
		auto c1 = co::Co{work_loop, "<X, " + std::to_string(i) + ">: ", std::ref(g_count)};
		auto c2 = co::Co{work_loop, "<Y, " + std::to_string(i) + ">: ", std::ref(g_count)};
		vec.push_back({std::move(c1), std::move(c2)});
	}

	for (auto & [c1, c2] : vec)
	{
		c1.await();
		c2.await();
	}

	end_of_test();
}

void test()
{
	std::cout << "test 1" << std::endl;
	//basic_test();
	dyn_alloc_test();
}
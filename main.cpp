#include <iostream>
#include <memory_resource>
#include <stack>
#include <map>
#include <thread>
#include <vector>

#include "./include/Coroutine.h"
#include "test/include/test.h"
#include "../data_structure/include/ListLockFree.h"

void list_free_test()
{
	ListLockFree<int> list;
	auto test = [&list] () 
	{
		struct info
		{
			ListLockFree<int>::iterator iter;
			bool erase;
		};

		std::vector<info> vec{};
		for (int i = 0; i < 10000; i++)
		{
			vec.push_back({list.push_back(i), true});

			int erase_idx = rand() % vec.size();
			if (vec[erase_idx].erase)
			{
				list.try_erase(vec[erase_idx].iter);
				vec[erase_idx].erase = false;
			}
		}

		for (int i = 0; i < 10000; i++)
		{
			if (vec[i].erase)
				list.try_erase(vec[i].iter);
		}
	};

	std::vector<std::thread> vec{};
	for (int i = 0; i < 8; i++)
		vec.emplace_back(std::thread{test});

	for (auto & t : vec)
		t.join();

	std::cout << list.size() << std::endl;
}

int main()
{
	std::cout << "main entry" << std::endl;

	//list_free_test();

	co::init();
    std::cout << "coroutine initilization compelete" << std::endl;
	test();

	/*
	SkipListLockFree<int, std::greater<>> sk;
	for (int i = 0; i < 100; i++)
		sk.push(i);

	sk.push(99);

	while (sk.size() > 0)
	{
		auto top = sk.front();
		sk.erase(top);

		std::cout << top << std::endl;
	}
    return 0;
    */
}
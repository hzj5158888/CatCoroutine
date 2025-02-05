#include <iostream>
#include <bitset>
#include <thread>
#include <vector>

#include "./include/Coroutine.h"
#include "test/include/test.h"
#include "../data_structure/include/BitSetLockFree.h"
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

void bitset_test()
{
	std::cout << "bitset test" << std::endl;

	constexpr auto bits = 1024;
	BitSetLockFree<bits> bs;

	std::vector<int> idx;
	for (int i = 0; i < bits; i++)
	{
		if (rand() & 1)
		{
			bs.set(i, true);
			idx.push_back(i);
		}
	}

	for (auto i : idx)
	{
		assert(bs.get(i));
	}

	std::cout << idx.size() << std::endl;

	for (auto i : idx)
	{
		std::cout << i << std::endl;
		auto cur = bs.change_first_expect(true, false);
		assert(cur == i);
	}
}

int main()
{
	std::cout << "main entry" << std::endl;

	//list_free_test();
	//bitset_test();

	co::init();
    std::cout << "coroutine initilization compelete" << std::endl;
	test();
}
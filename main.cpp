#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "./include/Coroutine.h"
#include "spin_lock.h"
#include "test/include/test.h"
#include "../data_structure/include/BitSetLockFree.h"

/*
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
 */

void bitset_test()
{
	std::cout << "bitset test" << std::endl;

	constexpr auto bits = 409600;

	BitSetLockFree<bits> bs;
	std::vector<int> idx;
	spin_lock m_lock{};
	std::vector<std::thread> ts;
	std::atomic<int> push_idx{};
	auto push = [&idx, &m_lock, &bs, &push_idx]()
	{
		int cur_idx{};
		while ((cur_idx = push_idx.fetch_add(1)) < bits)
		{
			if (rand() & 1)
				continue;

			assert(cur_idx >= 0 && cur_idx < bits);
			bs.set(cur_idx, true);
			std::lock_guard lock(m_lock);
			idx.push_back(cur_idx);
		}
	};
	for (int i = 0; i < 6; i++)
		ts.emplace_back(push);
	for (int i = 0; i < 6; i++)
		ts[i].join();

	ts.clear();
	std::sort(idx.begin(), idx.end());

	for (auto i : idx)
		assert(bs.get(i));

	std::cout << idx.size() << std::endl;

	std::vector<int> res{};
	std::atomic<int> max_cnt = 0;
	auto pick = [&res, &m_lock, &bs, &max_cnt]()
	{
		while (true) 
		{
			auto cur = bs.change_first_expect(true, false);
			if (cur == BitSetLockFree<>::INVALID_INDEX)
				break;
			
			std::lock_guard lock(m_lock);
			res.push_back(cur);
			//std::cout << cur << std::endl;
			assert(!bs.get(cur));
			assert(!bs.compare_set(cur, true, false));
		}
	};

	for (int i = 0; i < 6; i++)
		ts.emplace_back(pick);
	for (int i = 0; i < 6; i++)
		ts[i].join();

	ts.clear();
	std::sort(res.begin(), res.end());

	assert(res.size() == idx.size());

	for (size_t i = 0; i < res.size(); i++)
	{
		if (res[i] != idx[i])
		{
			std::cout << res[i] << ' ' << idx[i] << std::endl;
			assert(false);
		}
	}

	std::cout << "bitset test success" << std::endl;
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
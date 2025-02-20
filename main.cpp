#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "./include/Coroutine.h"
#include "spin_lock.h"
#include "test/include/test.h"
#include "../data_structure/include/BitSetLockFree.h"
#include "../data_structure/include/QueueLockFree.h"
#include "Context.h"

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

void queue_lock_free_test()
{
    QueueLockFree<int> q;
    constexpr int data_count = 10000000;
    auto push_elem = [&q]()
    {
        for (int i = 0; i < data_count; i++)
            q.push(i);
    };

    spin_lock m_lock{};
    std::vector<int> res{};
    auto pull_elem = [&q, &res, &m_lock]()
    {
        for (int i = 0; i < data_count; i++)
        {
            std::lock_guard lock(m_lock);
            auto top = q.try_pop();
            assert(top.has_value());
            res.push_back(top.value());
        }
    };

    assert(q.empty());
    res.clear();

    int cpu_count = 8;
    std::vector<std::thread> thread_vec{};
    for (int i = 0; i < cpu_count; i++)
    {
        thread_vec.emplace_back(push_elem);
        thread_vec.emplace_back(pull_elem);
    }
    for (auto & v : thread_vec)
        v.join();

    std::sort(res.begin(), res.end());

    int idx = 0;
    while (idx < (int)res.size())
    {
        for (int i = 0; i < cpu_count; i++)
        {
            std::cout << res[i + idx] << " ";
            assert(res[i + idx] == res[idx]);
        }

        idx += cpu_count;
    }

    std::cout << std::endl;
    std::cout << "res_size: " << res.size() << std::endl;
    std::cout << std::endl;
}

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
	auto pick = [&res, &m_lock, &bs]()
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
    //queue_lock_free_test();
	//bitset_test();

	co::init();
    std::cout << "coroutine initilization compelete" << std::endl;
	test();
}
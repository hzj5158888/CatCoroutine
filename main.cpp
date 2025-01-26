#include <iostream>
#include <stack>
#include <map>

#include "./include/Coroutine.h"
#include "test/include/test.h"
#include "./data_structure/include/SkipList.h"
#include "./data_structure/include/SkipListLockFree.h"

int main()
{
	std::cout << "main entry" << std::endl;

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
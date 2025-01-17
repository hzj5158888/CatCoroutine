#include <iostream>
#include <thread>
#include <memory>
#include <map>
#include <deque>
#include <stack>
#include <unordered_map>
#include <functional>
#include <utility>

#include "./include/Coroutine.h"
#include "./include/CoPrivate.h"
#include "test/include/test.h"

void t(int a, int b, int c)
{
	for (int i = 0; i < a; i++)
	{
		std::cout << "thread id = " << a << std::endl;
		std::cout << b << ' ' << c << std::endl;
		std::printf("b=%d, c=%d\n", b, c);
	}
}

void test0()
{
	std::vector<co::Co<co::PRIORITY_NORMAL>> vec{};
	for (int i = 0; i < 128; i++)
		vec.emplace_back(t, i, 2, 3);

	for (auto & i : vec)
		i.await();
}

int main()
{
	co::init();

    std::cout << "main entry" << std::endl;

    std::cout << sizeof (std::stack<int>) << std::endl;
    std::cout << sizeof (std::string) << std::endl;
    std::cout << sizeof (std::string_view) << std::endl;

	test();

    return 0;
}
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

void t(int a, int b, int c)
{
    std::cout << a << ' ' << b << ' ' << c << std::endl;
}

void test(void * arg)
{
    std::cout << "test, arg=" << arg << std::endl;
}

int main()
{
	co::init();

    std::cout << "main entry" << std::endl;

    std::cout << sizeof (std::stack<int>) << std::endl;
    std::cout << sizeof (std::string) << std::endl;
    std::cout << sizeof (std::string_view) << std::endl;

    co::Co c{t, 1, 2, -1};
	co::Co{t, 1, 2, 3}.await();
	c.await();

    return 0;
}
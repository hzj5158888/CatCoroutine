#include <iostream>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include <string>
#include <thread>

#include "./mem_pool/include/MemoryPool.h"
#include "./include/coroutine.hpp"

int main()
{
    co::Co<int> c{[]() -> void {}, 1, 2};

    co::suspend();
    co::yield(12);

    return 0;
}
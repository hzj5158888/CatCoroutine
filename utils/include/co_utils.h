//
// Created by hzj on 25-1-10.
//

#ifndef COROUTINE_CO_UTILS_H
#define COROUTINE_CO_UTILS_H

#include <cstdint>

namespace co {
    void wrap(void (*func)(void*), void * arg);
}

#endif //COROUTINE_CO_UTILS_H

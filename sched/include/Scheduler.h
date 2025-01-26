//
// Created by hzj on 25-1-11.
//

#ifndef COROUTINE_SCHEDULER_H
#define COROUTINE_SCHEDULER_H

#include <chrono>

#include "../../include/CoPrivate.h"


class Scheduler
{
public:
	virtual ~Scheduler() = default;

	virtual void run(Co_t * co) {}
	//virtual Co_t * interrupt() { return nullptr; }
};

#endif //COROUTINE_SCHEDULER_H

//
// Created by hzj on 25-1-11.
//

#include "include/CoPrivate.h"

using namespace co;

#ifdef __SCHED_CFS__
bool Co_t::operator > (const Co_t & oth) const 
{  
    sched.prefetch();
    oth.sched.prefetch();
    asm volatile("" ::: "memory");
    return sched.priority() > oth.sched.priority();
}

bool Co_t::operator < (const Co_t & oth) const 
{ 
    sched.prefetch();
    oth.sched.prefetch();
    asm volatile("" ::: "memory");
    return sched.priority() < oth.sched.priority();
}
#endif
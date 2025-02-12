//
// Created by hzj on 25-1-11.
//

#include "include/CoPrivate.h"

#ifdef __SCHED_CFS__
bool Co_t::operator > (const Co_t & oth) const 
{  
    sched.prefetch_v_runtime();
    oth.sched.prefetch_v_runtime();
    asm volatile("" ::: "memory");
    return sched.priority() > oth.sched.priority();
}

bool Co_t::operator < (const Co_t & oth) const 
{ 
    sched.prefetch_v_runtime();
    oth.sched.prefetch_v_runtime();
    asm volatile("" ::: "memory");
    return sched.priority() < oth.sched.priority();
}
#endif
//
// Created by hzj on 25-1-11.
//

#include "include/CoPrivate.h"

Co_t::Co_t() {}

bool Co_t::operator > (const Co_t & oth) const 
{  
    if (sched.priority() != oth.sched.priority()) [[likely]]
        return sched.priority() > oth.sched.priority();
    
    return id > oth.id;
}

bool Co_t::operator < (const Co_t & oth) const 
{ 
    sched.prefetch_v_runtime();
    oth.sched.prefetch_v_runtime();
    asm volatile("" ::: "memory");
    if (sched.priority() != oth.sched.priority()) [[likely]]
        return sched.priority() < oth.sched.priority();
    
    return id < oth.id;
}
//
// Created by hzj on 25-1-11.
//

#include <memory>

#include "include/CoPrivate.h"
#include "sched/include/CfsSched.h"

Co_t::Co_t() : sched(std::make_unique<CfsSchedEntity>()) {}
bool Co_t::operator > (const Co_t & oth) const { return sched->priority() > oth.sched->priority(); }
bool Co_t::operator < (const Co_t & oth) const { return sched->priority() < oth.sched->priority(); }
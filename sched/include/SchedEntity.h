#pragma once

#include <cstdint>
#include <chrono>

#include "../../utils/include/utils.h"

namespace co {
    class SchedEntity {
    public:
        bool can_migration{true};
        int occupy_thread{-1};

        virtual ~SchedEntity() = default;

        SchedEntity() {
#ifdef __STACK_STATIC__
            can_migration = false;
#endif
        }

        virtual void start_exec() {}

        virtual void end_exec() {}
    };
}
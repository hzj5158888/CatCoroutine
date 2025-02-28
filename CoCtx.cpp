#include "./include/CoCtx.h"

namespace co {
    namespace co_ctx
    {
        bool is_init{false};
        std::shared_ptr<SchedManager> manager{};
        std::atomic<uint32_t> coroutine_count{co::MAIN_CO_ID};
        std::shared_ptr<GlobalAllocatorGroup> g_alloc{};
#ifdef __DEBUG_SCHED__
        std::unordered_set<Co_t*> co_vec{};
        std::unordered_multiset<Co_t*> running_co{};
        spin_lock m_lock{};
#endif
        thread_local std::shared_ptr<local_t> loc{};
    }
}

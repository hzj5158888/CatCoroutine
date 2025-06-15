#include "./include/CoCtx.h"

namespace co::co_ctx
{
    bool is_init{};
    std::shared_ptr<SchedManager> manager{};
    AllocatorGroup * g_alloc{};
    TSCNS clock{};
    std::shared_ptr<Timer> timer{};
    std::shared_ptr<Epoller> epoller{};
#ifdef __DEBUG_SCHED__
    std::unordered_set<Co_t*> co_vec{};
    std::unordered_multiset<Co_t*> running_co{};
    spin_lock removal_lock{};
#endif
    thread_local std::shared_ptr<local_t> loc{};
}

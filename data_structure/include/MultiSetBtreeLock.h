#pragma once

#include "btree_set.h"
#include "../../utils/include/spin_lock_sleep.h"

namespace co {
    template<typename T, typename CMP = std::less<T>, int block_size = 256>
    class alignas(__CACHE_LINE__) MultiSetBtreeLock
    {
    private:
        using set_t = btree::multiset<T, CMP, std::allocator<T>, block_size>;

        set_t m_data{};
        spin_lock_sleep m_lock{};
        size_t m_size{};

        std::atomic<size_t> * get_size() { return reinterpret_cast<std::atomic<size_t>*>(&m_size); }
    public:
        template<class F>
        void insert(F && x)
        {
            std::lock_guard lock{m_lock};
            m_data.insert(x);
            m_size++;
        }

        template<class F>
        bool erase(F && x)
        {
            std::lock_guard lock(m_lock);

            auto iter = m_data.find(x);
            if (iter == m_data.end())
                return false;

            m_data.erase(iter);
            m_size--;
            return true;
        }

        void erase(set_t::const_iterator iter)
        {
            std::lock_guard lock(m_lock);
            m_data.erase(iter);
            m_size--;
        }

        T wait_and_pop()
        {
            while (true)
            {
                if (empty())
                    continue;

                std::lock_guard lock(m_lock);
                if (m_data.empty())
                    continue;

                T ans = *m_data.begin();
                m_data.erase(m_data.begin());
                m_size--;
                return ans;
            }
        }

        std::optional<T> try_pop()
        {
            std::lock_guard lock(m_lock);
            if (m_data.empty())
                return std::nullopt;

            T ans = *m_data.begin();
            m_data.erase(m_data.begin());
            m_size--;
            return ans;
        }

        size_t size() { return get_size()->load(std::memory_order_acquire); }
        bool empty() { return size() == 0; }
    };
}
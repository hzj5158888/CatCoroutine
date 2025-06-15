#pragma once

#include <set>

#include "../../utils/include/spin_lock_sleep.h"

namespace co {
    template<typename T, typename CMP = std::less<T>>
    class alignas(__CACHE_LINE__) MultiSetLock
    {
    public:
        using set_t = std::multiset<T, CMP>;
    private:
        set_t m_data{};
        spin_lock_sleep m_lock{};
        size_t m_size{};

        std::atomic<size_t> * get_size() { return reinterpret_cast<std::atomic<size_t>*>(&m_size); }
    public:
        template<class F>
        set_t::const_iterator insert(F && x)
        {
            std::lock_guard lock{m_lock};
            m_size++;
            return m_data.insert(x);
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

        template<class F, class Fn>
        bool erase_then(F && x, Fn && callback)
        {
            static_assert(std::is_invocable_v<Fn> || std::is_invocable_v<Fn, bool>);

            std::lock_guard lock(m_lock);
            auto iter = m_data.find(x);
            if (iter == m_data.end())
            {
                if constexpr (std::is_invocable_v<Fn, bool>)
                    callback(false);

                return false;
            }

            m_data.erase(iter);
            m_size--;

            if constexpr (std::is_invocable_v<Fn, bool>)
                callback(true);
            else
                callback();

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

    template<typename T, typename CMP = std::less<T>>
    class alignas(__CACHE_LINE__) MultiSetPmrLock
    {
    private:
        using set_t = std::pmr::multiset<T, CMP>;

        set_t m_data{&pool};
        spin_lock_sleep m_lock{};
        std::pmr::unsynchronized_pool_resource pool{get_default_pmr_opt()};
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

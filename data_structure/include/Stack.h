#pragma once

#include <vector>

namespace co {
    template<typename T>
    class Stack
    {
    private:
        std::vector<T> m_data{};
    public:
        template<class F>
        void push(F && x)
        {
            m_data.push_back(x);
        }

        template<class Iterator>
        void push_all(const Iterator begin, const Iterator end)
        {
            m_data.insert(m_data.end(), begin, end);
        }

        bool pop(T & x)
        {
            if (empty())
                return false;

            x = std::move(m_data.back());
            m_data.pop_back();
            return true;
        }

        bool pop()
        {
            if (empty())
                return false;

            m_data.pop_back();
            return true;
        }

        bool front(T & x)
        {
            if (empty())
                return false;

            x = m_data.front();
            return true;
        }

        bool back(T & x)
        {
            if (empty())
                return false;

            x = m_data.back();
            return true;
        }

        [[nodiscard]] size_t size() const { return m_data.size(); }

        [[nodiscard]] bool empty() const { return m_data.empty(); }
    };
}
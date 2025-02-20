#pragma once

#include <utility>
#include <vector>

#include "../../utils/include/utils.h"

template<typename T, typename CMP = std::less<T>>
class QuaternaryHeap
{
private:
    /* x := [0, 3] */
    constexpr int get_son(int fa, int x) { return ((fa - 1) << 2) + 2 + x; }
    constexpr int get_fa(int son) { return (son + 2) >> 2; }

    void pushup(int u)
    {
        while (true) 
        {
            int fa = get_fa(u);
            if (!fa || m_cmp(m_data[fa], m_data[u]))
                break;

            u = fa;
        }
    }

    void pushdown(int u)
    {
        int cur_size = (int)m_data.size();
        while (true)
        {
            int son = -1;
            for (int i = 0; i < 4; i++)
            {
                int cur_son = get_son(u, i);
                if (UNLIKELY(cur_son > (cur_size >> 2)))
                    return;

                if (!m_cmp(m_data[u], m_data[cur_son]))
                {
                    son = cur_son;
                    std::swap(m_data[u], m_data[cur_son]);
                    break;
                }
            }

            if (son == -1)
                break;

            u = son;
        }
    }

    std::vector<T> m_data{1};
    CMP m_cmp{};
public:
    QuaternaryHeap() = default;
    explicit QuaternaryHeap(int size) { m_data.reserve(size + 1); }

    void pop()
    {
        DASSERT(!empty());
        std::swap(m_data[1], m_data.back());
        m_data.pop_back();
		if (LIKELY(!empty()))
			pushdown(1);
    }

    template<typename F>
    void push(F data)
    {
        m_data.push_back(std::forward<F>(data));
        pushup(m_data.size() - 1);
    }

    template<typename F>
    void push_all(const std::vector<F> & data)
    {
        if (UNLIKELY(data.empty()))
            return;

        int start_idx = (int)m_data.size();
        m_data.insert(m_data.end(), data.begin(), data.end());
        for (int i = start_idx; i < (int)m_data.size(); i++)
            pushup(i);
    }

    T top() const { DASSERT(!empty()); return m_data[1]; }
    [[nodiscard]] int size() const { return (int)m_data.size() - 1; }
    [[nodiscard]] bool empty() const { return (int)m_data.size() - 1 <= 0; }
};
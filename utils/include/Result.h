#pragma once

#include <utility>

namespace co {
    class ResultBase
    {
    public:
        virtual ~ResultBase() = default;
    };

    template<typename T>
    class Result : public ResultBase
    {
    public:
        T result;

        Result() = delete;

        explicit Result(const T & ret) : result(std::forward<T>(ret)) {}

        explicit Result(T && ret) : result(std::move(ret)) {}
    };
}

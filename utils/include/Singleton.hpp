#pragma once

namespace co {
    template<typename T, typename F>
    class Singleton
    {
    public:
        Singleton() = delete;
        ~Singleton() = delete;
        Singleton(const Singleton &) = delete;
        Singleton & operator = (const Singleton &) = delete;

        static T & get()
        {
            static T obj{};
            return obj;
        }

        static F & loc()
        {
            static thread_local F obj{};
            return obj;
        }
    };
}
#pragma once

#include <type_traits>

#include <thread>

namespace co {

void suspend() {}

template<typename T>
void yield(const T & ret) {}

template<typename T>
class Co
{
private:
    void * handle;
public:
    template<typename Fn, typename... Args> explicit
    Co(Fn && fn, Args &&... args);
    
    ~Co();

    T await();
};

template<typename T>
template<typename Fn, typename... Args>
Co<T>::Co(Fn && fn, Args &&... args) {}

template<typename T>
Co<T>::~Co() {}

template<typename T>
T Co<T>::await() { return T(); }

}
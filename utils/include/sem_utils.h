//
// Created by hzj on 25-1-14.
//

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <semaphore.h>

class CountingSemCreateException : public std::exception {
public:
  [[nodiscard]] const char *what() const noexcept override {
    return "counting_sem construct failed";
  }
};

class CountingSemModifyException : public std::exception 
{
public:
    int err_code{};

    CountingSemModifyException(int code) : err_code(code) {}

    [[nodiscard]] const char * what() const noexcept override 
    {
        return "counting_sem Modify failed";
    }
};

class counting_semaphore {
private:
  sem_t sem{};
public:
  explicit inline counting_semaphore(uint32_t count = 0);
  ~counting_semaphore();
  inline void wait();
  inline void signal();
  inline void signal(uint32_t count);
  inline bool try_wait();
};

counting_semaphore::counting_semaphore(uint32_t count) {
  int res = sem_init(&sem, 0, count);
  if (res != 0)
    throw CountingSemCreateException();
}

inline counting_semaphore::~counting_semaphore() { sem_destroy(&sem); }

inline void counting_semaphore::wait() 
{ 
    int res = sem_wait(&sem);
    if (res != 0)
      throw CountingSemModifyException(res);
}

inline void counting_semaphore::signal() 
{ 
    int res = sem_post(&sem);
    if (res != 0)
      throw CountingSemModifyException(res);
}

inline void counting_semaphore::signal(uint32_t count) 
{ 
    for (; count != 0; count--)
      signal();
}

inline bool counting_semaphore::try_wait() { return sem_trywait(&sem) == 0; }
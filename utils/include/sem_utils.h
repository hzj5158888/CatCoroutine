//
// Created by hzj on 25-1-14.
//

#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
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

class counting_semaphore {
private:
  sem_t sem{};
public:
  explicit inline counting_semaphore(uint32_t count = 0);
  ~counting_semaphore();
  inline void wait();
  inline void signal();
};

counting_semaphore::counting_semaphore(uint32_t count) {
  int res = sem_init(&sem, 0, count);
  if (res != 0)
    throw CountingSemCreateException();
}

inline counting_semaphore::~counting_semaphore() { sem_destroy(&sem); }

inline void counting_semaphore::wait() { sem_wait(&sem); }

inline void counting_semaphore::signal() { sem_post(&sem); }
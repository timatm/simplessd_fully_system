//
//  timer.h
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#ifndef YCSB_C_TIMER_H_
#define YCSB_C_TIMER_H_


#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <chrono>

namespace utils {


// portable_timer.h
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
class PortableTimer {
public:
  void Start() {
    started_ = true;
    kind_ = pick_kind_();                 // 只选一次
    t0_ = now_of_kind_(kind_);            // 读取一次起点
  }
  double End() const {
    if (!started_) return 0.0;
    Stamp t1 = now_of_kind_(kind_);       // 用同一种钟读取终点
    if (kind_ == K::POSIX)  return (t1.s - t0_.s) + (t1.ns - t0_.ns) / 1e9;
    if (kind_ == K::GTOD)   return (t1.s - t0_.s) + (t1.us - t0_.us) / 1e6;
    long hz = sysconf(_SC_CLK_TCK);
    return (hz > 0) ? double(t1.ticks - t0_.ticks) / double(hz) : 0.0;
  }
  static double Sanity200ms() { /* 你原来的实现保持 */ }

private:
  enum class K { POSIX, GTOD, TICKS };
  struct Stamp { K kind; int64_t s=0, ns=0, us=0; clock_t ticks=0; };

  static K pick_kind_() {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return K::POSIX;
    if (clock_gettime(CLOCK_REALTIME,  &ts) == 0) return K::POSIX;
    timeval tv{};
    if (gettimeofday(&tv, nullptr) == 0) return K::GTOD;
    return K::TICKS;
  }
  static Stamp now_of_kind_(K k) {
    Stamp st{}; st.kind = k;
    if (k == K::POSIX) {
      timespec ts{};
      if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0 &&
          clock_gettime(CLOCK_REALTIME,  &ts) != 0) {
        // 不应该发生：降级以免 0
        st.kind = K::GTOD; return now_of_kind_(st.kind);
      }
      st.s = ts.tv_sec; st.ns = ts.tv_nsec; return st;
    } else if (k == K::GTOD) {
      timeval tv{};
      if (gettimeofday(&tv, nullptr) != 0) { st.kind = K::TICKS; return now_of_kind_(st.kind); }
      st.s = tv.tv_sec; st.us = tv.tv_usec; return st;
    } else {
      tms buf{};
      st.ticks = times(&buf); return st;
    }
  }

  bool started_ = false;
  K kind_ = K::POSIX;
  Stamp t0_{};
};




// 单调时钟计时器（纳秒→秒）
class MonotonicTimer {
public:
  void Start() { clock_gettime(CLOCK_MONOTONIC, &t0_); }
  double End() const {
    timespec t1{};
    clock_gettime(CLOCK_MONOTONIC, &t1);
    unsigned long long ns0 = (unsigned long long)t0_.tv_sec * 1000000000ull + (unsigned long long)t0_.tv_nsec;
    unsigned long long ns1 = (unsigned long long)t1.tv_sec * 1000000000ull + (unsigned long long)t1.tv_nsec;
    return (ns1 - ns0) / 1e9; // 秒
  }
private:
  timespec t0_{};
};

// 快速 200ms 自检：应当输出 ~0.200000 s
static inline double now_monotonic_s() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}
static inline void sanity_timer_200ms() {
  double t0 = now_monotonic_s();
  timespec req{0, 200*1000*1000}; // 200ms
  nanosleep(&req, nullptr);
  double t1 = now_monotonic_s();
  fprintf(stderr, "[sanity] 200ms sleep measured = %.6f s\n", t1 - t0);
}

// template <typename T>
class Timer {
 public:
  void Start() { started_ = true; t0_ = Clock::now(); }
  double End() const {
    if (!started_) return 0.0;
    return std::chrono::duration<double>(Clock::now() - t0_).count();  // 秒
  }
 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point t0_{};
  bool started_{false};
};


} // utils

#endif // YCSB_C_TIMER_H_


#pragma once

#include <queue>
#include <mutex>
#include <thread>
#include <iostream>
#include <atomic>

#include "TimeEntry.h"

namespace delay_queue
{
class SpinLock
{
  typedef size_t micro_seconds;
  std::atomic<bool> m_locked_flag = ATOMIC_VAR_INIT(false);
  micro_seconds m_SleepWhenAcquireFailedInMicroSeconds;

  SpinLock& operator=(const SpinLock) = delete;
  SpinLock(const SpinLock&) = delete;
  std::atomic<bool> locked_flag_ = ATOMIC_VAR_INIT(false);
public:
  SpinLock(micro_seconds _SleepWhenAcquireFailedInMicroSeconds = micro_seconds(-1))
	  :m_SleepWhenAcquireFailedInMicroSeconds(_SleepWhenAcquireFailedInMicroSeconds){}

  void lock()
  {
    bool exp = false;
    while (!locked_flag_.compare_exchange_strong(exp, true)) {
      exp = false;
      if (m_SleepWhenAcquireFailedInMicroSeconds == size_t(-1)) {
        std::this_thread::yield();
      } else if (m_SleepWhenAcquireFailedInMicroSeconds != 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(m_SleepWhenAcquireFailedInMicroSeconds));
      }
    }
  }
  void unlock()
  {
    locked_flag_.store(false);
  }
};



struct delay_queue_entry{
  m_timeval dqe_expiration;
  void* appendix;
  delay_queue_entry(void* appe, m_timeval expiration)
  :appendix(appe),
  dqe_expiration(expiration)
  {};
  
  inline bool operator>(const delay_queue_entry& rdqe) const{
            return dqe_expiration.__node.tv_sec*1000000 + dqe_expiration.__node.tv_usec 
            > rdqe.dqe_expiration.__node.tv_sec*1000000 + rdqe.dqe_expiration.__node.tv_usec;     
  }
};


template<typename T>
class DelayQueue
{
 public:
  T* poll(){
    // lock_guard<mutex> lock(m_mutex);
    std::unique_lock<SpinLock> lock(m_spinLock);
    if(!m_q.empty()){
      delay_queue_entry first = m_q.top();
      if(getDelay(first)) return nullptr;
      else{
        m_q.pop();
        return static_cast<T*>(first.appendix);
      }
    }
    return nullptr;
  }
  // T take();
  bool offer(T* t, m_timeval expiration){
    // lock_guard<mutex> lock(m_mutex);
    std::unique_lock<SpinLock> lock(m_spinLock);
    m_q.emplace(t, expiration);
    return true;
  }


 private:
  bool getDelay(const delay_queue_entry& e){
    m_timeval tmp_now;
    tmp_now.getTimeOfDay();
    return e.dqe_expiration > tmp_now;
  }
  SpinLock m_spinLock;
  std::priority_queue<delay_queue_entry,vector<delay_queue_entry>,greater<delay_queue_entry>> m_q;
  // mutex m_mutex;
};
}





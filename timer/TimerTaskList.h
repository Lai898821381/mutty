#pragma once

#include <mutex>
#include <atomic>
#include "delay_queue/TimeEntry.h"
#include <functional>
#include <memory>

namespace timer
{
  class Timer;
  class TimingWheel;
  class TimerTaskEntry;
  class TimerTaskList;

  class TimerTask
  {
   public:
    template <typename Func>
    explicit TimerTask(Func f)
    :m_timerTaskEntry(nullptr),
     m_func(f)
     {}
    std::function<void()> getFunc(){
      return m_func;
    }
    void setTimerTaskEntry(TimerTaskEntry* entry);
    TimerTaskEntry* getTimerTaskEntry();

   private:
    std::function<void()> m_func;
    TimerTaskEntry* m_timerTaskEntry;
    mutex mutex_;
  };

  class TimerTaskEntry{
    m_timeval m_expiration;
    TimerTask* m_timerTask;
   public:
    TimerTaskEntry(TimerTask* timerTask, const m_timeval& expiration);
    TimerTaskEntry(const TimerTaskEntry* timerTaskEntry);
    bool cancelled();
    void remove();
    int compare(TimerTaskEntry* TTEntry);
    TimerTask* getTimerTask();
    const m_timeval getExpiration();

    TimerTaskList* list = nullptr;
    TimerTaskEntry* prev = nullptr;
    TimerTaskEntry* next = nullptr;
  };

  class TimerTaskList
  {
   public:
    TimerTaskList();
    ~TimerTaskList(){
      auto itd = root->next, itn = itd;
      while(itd != root.get()){
        itn = itd->next;
        delete itd;
        itd = itn;
      }
    }
    TimerTaskList(const TimerTaskList& timerTaskList);
    TimerTaskList& operator=(const TimerTaskList& timerTaskList);

    bool setExpiration(m_timeval expiration);
    const m_timeval getExpiration();
    void add(TimerTaskEntry* timerTaskEntry);
    void remove(TimerTaskEntry* timerTaskEntry);
    void flush(Timer* callBackTimer);
    int compareTo(TimerTaskEntry* timerTaskEntry);
   private:
    std::shared_ptr<std::recursive_mutex> m_mtx;
    std::atomic<int> taskCounter;
    std::shared_ptr< TimerTaskEntry > root;
    std::shared_ptr< std::atomic_flag > m_setFlag; 
    m_timeval m_expiration;
  };
}

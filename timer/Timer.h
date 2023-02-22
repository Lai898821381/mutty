#pragma once

#include <mutex>
#include <shared_mutex>
#include <vector>
#include <atomic>
#include <thread>
#include "delay_queue/TimeEntry.h"
#include "delay_queue/DelayQueue.h"
#include "thread_pool/ThreadPool.h"
#include "TimerTaskList.h"
#include "TimingWheel.h"

namespace timer
{
  class TimerTask;
  class TimerTaskList;
  class TimerTaskEntry;
  class TimingWheel;

  class Timer
  {
   public:

    explicit Timer(m_timeval tickMs, int wheelSize,
		    m_timeval timeout = m_timeval(20000), int threadNum = 4);
    ~Timer();
    void advanceClock();
    void shutdown();
    void start();
    void startTick();
    
    void addTimerTaskEntry(TimerTaskEntry* timerTaskEntry);

    /**
     * addByDelay的函数说明
     * 
     * @brief 用于向定时器加入延时任务，如果延时时间到，则执行该任务。
     * @param func 参数模板，接收任意函数名
     * @param args 可变模板参数，接收待传入func函数的所有参数
     * @param delayTime 延时时间，类型是m_timeval
     *
    */
    template<typename Func, typename ... Args>
    void addByDelay(const m_timeval& delayTime, const Func& func, Args&& ... args)
    {
      auto f = std::bind(func, std::forward<Args>(args)...);
      TimerTask* timertask = new TimerTask(f);
      m_timeval tmp_now;
      tmp_now.getTimeOfDay();
      add(timertask, tmp_now + delayTime);
    }

    static Timer * newTimer(m_timeval tickMs, int wheelSize,
                            m_timeval timeout = m_timeval(20000), int threadNum = 4);
   private:
    void add(TimerTask* timerTask, m_timeval expiration);

    std::unique_ptr<thread_pool::ThreadPool> m_threadPool;
    std::shared_ptr<delay_queue::DelayQueue<TimerTaskList>> m_delayQueue;
    std::atomic<int> m_threadNum;
    std::shared_ptr<TimingWheel> m_timingWheel;
    m_timeval m_tickMs;
    m_timeval m_startMs;
    m_timeval m_timeout;
    std::atomic<bool> isRunning={false};
    int m_wheelSize;
    static Timer* defaultTimer;
    mutable std::shared_mutex m_rwMutex;
  };
}

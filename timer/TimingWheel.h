#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include "delay_queue/DelayQueue.h"
#include "delay_queue/TimeEntry.h"
#include <iostream>
namespace timer
{
  class TimerTask;
  class TimerTaskList;
  class TimerTaskEntry;

  class TimingWheel
  {
   public:
    TimingWheel(const m_timeval& tickMs, int wheelSize, const m_timeval& startMs,
		std::shared_ptr<delay_queue::DelayQueue<TimerTaskList>>& queue);
    ~TimingWheel();
    bool add(TimerTaskEntry* timerTaskEntry);
    void advanceClock(const m_timeval& timeMs);

   private:
    m_timeval m_interval;
    m_timeval m_tickMs;
    int m_wheelSize;
    m_timeval m_startMs;
    m_timeval m_currentTime;
    std::shared_ptr<delay_queue::DelayQueue<TimerTaskList>> m_queue;
    std::vector<TimerTaskList> m_buckets;
    TimingWheel* m_overflowWheel;
    std::mutex m_mutex;

    void addOverflowWheel();
  };
}

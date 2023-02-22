#include "Timer.h"
#include <iostream>
using namespace timer;

Timer::Timer(m_timeval tickMs, int wheelSize, m_timeval timeout, int threadNum)
:m_tickMs(tickMs),
m_wheelSize(wheelSize),
m_timeout(timeout),
m_threadPool(new thread_pool::ThreadPool(threadNum)),
m_delayQueue(new delay_queue::DelayQueue<TimerTaskList>())  
{
  m_startMs.getTimeOfDay();
  m_timingWheel = make_unique<TimingWheel>(m_tickMs,m_wheelSize,m_startMs,m_delayQueue);
}

Timer* Timer::defaultTimer = nullptr;

Timer* Timer::newTimer(m_timeval tickMs, int wheelSize,
                        m_timeval timeout , int threadNum )
{
    static std::once_flag flag;
    std::call_once(flag, [&]
    {
        assert(defaultTimer == nullptr);
        defaultTimer = new Timer(tickMs, wheelSize, timeout, threadNum);
        defaultTimer->start();
    });
    return defaultTimer;
}

Timer::~Timer(){
  shutdown();
  m_threadPool.reset();
  m_delayQueue.reset();
  m_timingWheel.reset();
}

void Timer::start()
{
  isRunning = true;
  m_threadPool->addTask(std::bind(&Timer::startTick, this));
}

void Timer::startTick()
{
  while(isRunning)
  {
    timeval timeout = m_timeout.getTimeval();
    select(0,NULL,NULL,NULL,&timeout);
    advanceClock();
  }
}

void Timer::add(TimerTask* timerTask, m_timeval expiration)
{
  std::shared_lock<std::shared_mutex> lock(m_rwMutex);
  addTimerTaskEntry(new TimerTaskEntry(timerTask, expiration));
}

void Timer::addTimerTaskEntry(TimerTaskEntry* timerTaskEntry)
{
  // 添加失败，说明已到期
  if(!m_timingWheel->add(timerTaskEntry)){
    if(!timerTaskEntry->cancelled()){
      TimerTask* timerTask = timerTaskEntry->getTimerTask();
      // m_threadPool->submit(timerTask->getFunc());
      m_threadPool->addTask(timerTask->getFunc());
      delete timerTaskEntry;
    }
  }
}

void Timer::advanceClock(){
  // 任务到期了才会poll出来
  auto bucket = m_delayQueue->poll();
  if(bucket != nullptr){
    // 有到期任务，不断循环
    std::unique_lock<std::shared_mutex> lock(m_rwMutex);
    // 级联更新各层级的时间轮的currtentTime为时间槽的过期时间
    while(bucket != nullptr){
      m_timingWheel->advanceClock(bucket->getExpiration());
      bucket->flush(this);
      bucket = m_delayQueue->poll();
    }
  }
}

void Timer::shutdown(){
  isRunning=false;
  m_threadPool->stop();
  std::cout<<"Timer has been stopped!";
}



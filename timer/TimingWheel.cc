#include "TimingWheel.h"
#include "TimerTaskList.h"

using namespace timer;

TimingWheel::TimingWheel(const m_timeval& tickMs, int wheelSize, const m_timeval& startMs,
			 std::shared_ptr<delay_queue::DelayQueue<TimerTaskList>>& queue)
:m_tickMs(tickMs),
m_wheelSize(wheelSize),
m_queue(queue),
m_overflowWheel(nullptr)
{
  m_interval = tickMs * wheelSize;
  m_buckets.resize(wheelSize, TimerTaskList());
  m_currentTime = startMs - (startMs % tickMs);
}

TimingWheel::~TimingWheel()
{
  if(m_overflowWheel != nullptr)
    delete m_overflowWheel;

}

void TimingWheel::addOverflowWheel(){
  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_overflowWheel == nullptr){
    m_overflowWheel = new TimingWheel(
      m_interval,
      m_wheelSize,
      m_currentTime,
      m_queue);
  }
}

bool TimingWheel::add(TimerTaskEntry* timerTaskEntry)
{
  // 未来的任务会被添加，当前或过期的任务不会被添加，返回失败，将直接被执行
  m_timeval expiration = timerTaskEntry->getExpiration();
  if(timerTaskEntry->cancelled()) return false;
  // 过期或刚好到期的任务
  else if (expiration < (m_currentTime + m_tickMs)) return false;
  // 任务可以放进当前时间轮
  else if (expiration < (m_currentTime + m_interval)){
    long virtualId = expiration / m_tickMs;
    TimerTaskList& bucket = m_buckets.at(virtualId % m_wheelSize);
    bucket.add(timerTaskEntry);
    // 判断bucket是否已经设置了过期时间，避免bucket被重复添加到queue中
    if(bucket.setExpiration(m_tickMs * virtualId)) 
      m_queue->offer(&bucket,expiration);
    return true;
  } else{
  // 需要将任务放入下一个时间轮中，递归
    // 创建下一层级的时间轮
    if (m_overflowWheel == nullptr) addOverflowWheel();
    return m_overflowWheel->add(timerTaskEntry);
  }
}

void TimingWheel::advanceClock(const m_timeval& timeMs){
  if (timeMs >= m_currentTime + m_tickMs) {
    m_currentTime = timeMs - (timeMs % m_tickMs);
    // 递归推进更高层级的时间轮
    if(m_overflowWheel != nullptr) m_overflowWheel->advanceClock(m_currentTime);
  }
}

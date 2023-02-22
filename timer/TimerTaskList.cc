#include "TimerTaskList.h"
#include "Timer.h"

using namespace timer;

TimerTaskEntry::TimerTaskEntry(TimerTask* timerTask, const m_timeval& expiration)
:m_expiration(expiration),
m_timerTask(timerTask)
{
   if(timerTask != nullptr)
     timerTask->setTimerTaskEntry(this);
}

TimerTaskEntry::TimerTaskEntry(const TimerTaskEntry* timerTaskEntry)
{
  m_expiration = timerTaskEntry->m_expiration;
  m_timerTask = timerTaskEntry->m_timerTask;
  list = timerTaskEntry->list;
  prev = timerTaskEntry->prev;
  next = timerTaskEntry->next;
}

bool TimerTaskEntry::cancelled(){
  return m_timerTask->getTimerTaskEntry() != this;
}

void TimerTaskEntry::remove(){
  TimerTaskList* currentList = list;
  while (currentList != nullptr){
    currentList->remove(this);
    currentList = list;
  }
}

TimerTask* TimerTaskEntry::getTimerTask()
{
  return m_timerTask;
}

const m_timeval TimerTaskEntry::getExpiration()
{
  return m_expiration;
}

TimerTaskList::TimerTaskList()
:m_mtx(new std::recursive_mutex),
m_expiration(m_timeval()),
m_setFlag(new std::atomic_flag(ATOMIC_FLAG_INIT)),
root(new TimerTaskEntry(nullptr, m_timeval()))
{
  root->next = root.get();
  root->prev = root.get();
}

TimerTaskList::TimerTaskList(const TimerTaskList& timerTaskList)
{
  m_mtx = timerTaskList.m_mtx;
  root = timerTaskList.root;
  root->next = timerTaskList.root->next;
  root->prev = timerTaskList.root->prev;
  m_setFlag = timerTaskList.m_setFlag;
  m_expiration = timerTaskList.m_expiration;
}

TimerTaskList& TimerTaskList::operator=(const TimerTaskList& timerTaskList)
{
  m_mtx = timerTaskList.m_mtx;
  root = timerTaskList.root;
  m_setFlag = timerTaskList.m_setFlag;
  m_expiration = timerTaskList.m_expiration;
}

void TimerTaskList::add(TimerTaskEntry* timerTaskEntry){
  timerTaskEntry->remove();
  std::unique_lock<std::recursive_mutex> lock(*m_mtx);
  if(timerTaskEntry->list == nullptr) {
    TimerTaskEntry* tail = root->prev;
    timerTaskEntry->next = root.get();
    timerTaskEntry->prev = tail;
    timerTaskEntry->list = this;
    tail->next = timerTaskEntry;
    root->prev = timerTaskEntry;
  }
}

void TimerTaskList::remove(TimerTaskEntry* timerTaskEntry){
  std::unique_lock<std::recursive_mutex> lock(*m_mtx);
  if(timerTaskEntry->list == this){
    timerTaskEntry->next->prev = timerTaskEntry->prev;
    timerTaskEntry->prev->next = timerTaskEntry->next;
    timerTaskEntry->next = nullptr;
    timerTaskEntry->prev = nullptr;
    timerTaskEntry->list = nullptr;
  }
}

// 删除对应的槽，并将槽中元素重新添加到时间轮，将会直接被执行
void TimerTaskList::flush(Timer* callBackTimer){
  std::unique_lock<std::recursive_mutex> lock(*m_mtx);
  TimerTaskEntry* head = root->next;
  auto f = std::bind(&Timer::addTimerTaskEntry,callBackTimer,std::placeholders::_1);
  while(head != root.get()){
    remove(head);
    // 执行传入的function，addTimerTaskEntry
    f(head);
    head = root->next;
  }
  while(!m_setFlag->test_and_set()){
    m_expiration = -1L;
    m_setFlag->clear();
    break;    
  }
}

bool TimerTaskList::setExpiration(m_timeval expiration){
    while(!m_setFlag->test_and_set()) {
        m_timeval oriExpiration = m_expiration;
        m_expiration = expiration;
        m_setFlag->clear();
        return expiration != oriExpiration;
    }
    return false;
}

const m_timeval TimerTaskList::getExpiration(){
    while(!m_setFlag->test_and_set()) {
        m_timeval expiration = m_expiration;
        m_setFlag->clear();
        return expiration;
    }
}

void TimerTask::setTimerTaskEntry(TimerTaskEntry* entry)
{
  if(m_timerTaskEntry != nullptr && m_timerTaskEntry != entry)
    m_timerTaskEntry->remove();
  m_timerTaskEntry = entry;
}

TimerTaskEntry* TimerTask::getTimerTaskEntry(){
  return m_timerTaskEntry;
}


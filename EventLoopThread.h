#ifndef MUTTY_EVENTLOOPTHREAD_H
#define MUTTY_EVENTLOOPTHREAD_H

#include <mutex>
#include <thread>
#include <string>
#include <future>
#include "base/noncopyable.h"

namespace mutty
{

  class EventLoop;

  class EventLoopThread : public noncopyable {
  public:
    explicit EventLoopThread(const std::string name = "EventLoopThread");
    ~EventLoopThread();

    EventLoop* getLoop() const
    {
      return loop_;
    }

    void run();

  private:
    EventLoop* loop_;
    std::string loopThreadName_;
    void threadFunc();
    std::promise<EventLoop*> promiseForLoopPointer_;
    std::promise<int> promiseForRun_;
    std::promise<int> promiseForLoop_;
    std::once_flag once_;
    std::thread thread_;
  };

}  // namespace mutty

#endif  // MUTTY_EVENTLOOPTHREAD_H


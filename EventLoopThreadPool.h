#ifndef MUTTY_EVENTLOOPTHREADPOOL_H
#define MUTTY_EVENTLOOPTHREADPOOL_H

#include "base/noncopyable.h"

#include <functional>
#include <memory>
#include <vector>

namespace mutty
{

  class EventLoop;
  class EventLoopThread;

  class EventLoopThreadPool : public noncopyable{
  public:
    EventLoopThreadPool(EventLoop* baseLoop,
                        size_t threadNum,
                        const std::string& name = "EventLoopThreadPool" );
    ~EventLoopThreadPool(){} // Don't delete loop, it's stack variable
    void start();

    size_t size() {
        return loopThreads_.size();
    }
    // valid after calling start()
    /// round-robin
    EventLoop* getNextLoop();

    std::vector<EventLoop*> getAllLoops() const;

  private:
    EventLoop* baseLoop_; //与Acceptor所属的eventloop相同
    size_t next_; //新连接到来Eventloop对应的下标
    //ptr_vector析构的时候会析构自己开辟出来的存放指针的空间,同时析构指针本身指向的空间而一般容器不会析构指针本身指向的空间
    std::vector<std::shared_ptr<EventLoopThread>> loopThreads_;
  };

}  // namespace mutty

#endif  // MUTTY_EVENTLOOPTHREADPOOL_H

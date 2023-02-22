#include "EventLoop.h"
#include "EventLoopThread.h"
#include "EventLoopThreadPool.h"

#include <stdio.h>

namespace mutty{
  EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, size_t threadNum, const std::string& name)
    : baseLoop_(baseLoop),
      next_(0)
  {
    for(int i = 0; i < threadNum; ++i){
      char buf[name.size() + 32];
      snprintf(buf, sizeof(buf), "%s%d", name.c_str(), i);
      loopThreads_.emplace_back(std::make_shared<EventLoopThread>(std::string (buf)));
    }
  }


  void EventLoopThreadPool::start()
  {
    baseLoop_->assertInLoopThread();

    for (int i = 0; i < loopThreads_.size(); ++i)
    {
      loopThreads_[i]->run();
    }
  }

  EventLoop* EventLoopThreadPool::getNextLoop()
  {
    baseLoop_->assertInLoopThread();
    if (!loopThreads_.empty())
    {
      // round-robin
      EventLoop *loop = loopThreads_[next_]->getLoop();
      ++next_;
      if (next_ >= loopThreads_.size())
      {
        next_ = 0;
      }
      return loop;
    }
    return nullptr;
  }

  std::vector<EventLoop*> EventLoopThreadPool::getAllLoops() const
  {
    baseLoop_->assertInLoopThread();
    std::vector<EventLoop *> ret;
    if (loopThreads_.empty())
    {
      ret.push_back(baseLoop_);
    }
    else
    {
      for(auto &loopThread : loopThreads_){
        ret.push_back(loopThread->getLoop());
      }
    }
    return ret;
  }
}
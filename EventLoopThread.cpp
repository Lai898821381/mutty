#include <functional>
#include <sys/prctl.h>

#include "EventLoopThread.h"
#include "EventLoop.h"

namespace mutty{
  //     thread_(std::bind(&EventLoopThread::threadFunc, this), name),
  //     里将执行loop.loop()绑定
  EventLoopThread::EventLoopThread(const std::string name)
    : loop_(nullptr),
      loopThreadName_(name),
      thread_(std::bind(&EventLoopThread::threadFunc, this))
  {
    auto f = promiseForLoopPointer_.get_future();
    loop_ = f.get();
  }

  EventLoopThread::~EventLoopThread()
  {
    if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
    {
      // still a tiny chance to call destructed object, if threadFunc exits just now.
      // but when EventLoopThread destructs, usually programming is exiting anyway.
      loop_->quit();
      thread_.join();
    }
  }

  void EventLoopThread::threadFunc()
  {
      ::prctl(PR_SET_NAME, loopThreadName_.c_str());
      EventLoop loop;
      loop.queueInLoop([this]() { promiseForLoop_.set_value(1); });
      promiseForLoopPointer_.set_value(&loop);
      auto f = promiseForRun_.get_future();
      (void)f.get();
      loop.loop();
      loop_ = NULL;
  }

  void EventLoopThread::run()
  {
      std::call_once(once_, [this]() {
          auto f = promiseForLoop_.get_future();
          promiseForRun_.set_value(1);
          // Make sure the event loop loops before returning.
          (void)f.get();
      });
  }
}
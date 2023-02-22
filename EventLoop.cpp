#include <iostream>
#include <algorithm>
#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "EventLoop.h"
#include "Channel.h"
#include "Poller.h"
#include "timer/delay_queue/TimeEntry.h"

namespace mutty {
  thread_local EventLoop *t_loopInThisThread = nullptr;

  const int kPollTimeMs = 10000;

  int createEventfd()
  {
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
      std::cout << "Failed in eventfd" << std::endl;
      abort();
    }
    return evtfd;
  }

  class IgnoreSigPipe
  {
  public:
    IgnoreSigPipe()
    {
      ::signal(SIGPIPE, SIG_IGN);
      // LOG_TRACE << "Ignore SIGPIPE";
    }
  };

  IgnoreSigPipe initObj;

  EventLoop* EventLoop::getEventLoopOfCurrentThread()
  {
    return t_loopInThisThread;
  }

  EventLoop::EventLoop()
    : looping_(false),
      threadId_(std::this_thread::get_id()),
      quit_(false),
      poller_(Poller::newDefaultPoller(this)),
      currentActiveChannel_(nullptr),
      eventHandling_(false),
      timer_(Timer::newTimer(m_timeval(200000),20)),
      callingPendingFuncs_(false),
      wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_))
  {
    if (t_loopInThisThread)
    {
      // 如果当前线程已经创建了EventLoop对象，终止(LOG_FATAL)
      std::cout << "There is already an EventLoop in this thread" << std::endl;
      exit(-1);
    }
    t_loopInThisThread = this;
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
  }

  EventLoop::~EventLoop()
  {
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
  }

  // void EventLoop::runAt(const m_timeval &time, TimerCallback cb)
  // {
  //   timer_->addTimer(std::move(cb), time, 0.0);
  // }

  // 同步，所以delay直接引用即可，临时对象
  void EventLoop::runAfter(const m_timeval &delay, TimerCallback cb)
  {
    timer_->addByDelay(delay, std::move(cb));
  }

  void EventLoop::updateChannel(Channel* channel)
  {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
  }

  void EventLoop::removeChannel(Channel* channel)
  {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    if (eventHandling_)
    {
      assert(currentActiveChannel_ == channel ||
          std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
    }
    poller_->removeChannel(channel);
  }

  void EventLoop::quit()
  {
    quit_ = true;
    if (!isInLoopThread())
    {
      wakeup();
    }
  }

  void EventLoop::loop()
  {
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;  // FIXME: what if someone calls quit() before loop() ?

    while (!quit_)
    {
      activeChannels_.clear();
      poller_->poll(kPollTimeMs, &activeChannels_);
      eventHandling_ = true;
      for (Channel* channel : activeChannels_)
      {
        currentActiveChannel_ = channel;
        currentActiveChannel_->handleEvent();
      }
      currentActiveChannel_ = nullptr;
      eventHandling_ = false;
      doPendingFunctors();
    }
    looping_ = false;
  }

  void EventLoop::runInLoop(Functor cb)
  {
    if (isInLoopThread())
    { 
      cb(); 
    }
    else
    {
      queueInLoop(std::move(cb));
    }
  }

  void EventLoop::queueInLoop(const Functor &cb)
  {
    funcs_.enqueue(cb);

    if (!isInLoopThread() || !callingPendingFuncs_) // ？
    {
      wakeup();
    }
  }

  bool EventLoop::hasChannel(Channel* channel)
  {
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return poller_->hasChannel(channel);
  }

  void EventLoop::abortNotInLoopThread()
  {
    quit_ = true;
    std::cout << "It is forbidden to run loop on threads other than event-loop thread" << std::endl;
    exit(1);
  }

  void EventLoop::wakeup()
  {
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
  }

  void EventLoop::handleRead()
  {
    uint64_t one;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n < 0)
        std::cout << "wakeup read error";
  }

  void EventLoop::doPendingFunctors()
  {
    // std::vector<Functor> functors;
    callingPendingFuncs_ = true;
    
    // {
    //   lock_guard<std::mutex> lock(mutex_);
    //   functors.swap(pendingFunctors_);
    // }

    // for (const Functor& functor : functors)
    // {
    //   functor();
    // }
    while (!funcs_.empty())
    {
        Functor functor;
        while (funcs_.dequeue(functor))
        {
            functor();
        }
    }

    callingPendingFuncs_ = false;
  }
}


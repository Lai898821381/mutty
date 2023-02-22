#ifndef MUTTY_EVENTLOOP_H
#define MUTTY_EVENTLOOP_H

#include <atomic>
#include <mutex>
#include <functional>
#include <vector>
#include <thread>
#include <memory>
#include "Callbacks.h"
#include "MpscQueue.h"
#include "base/noncopyable.h"
#include "timer/Timer.h"

using namespace timer;

namespace mutty {

  class Channel;
  class Poller;

  ///
  /// Reactor, at most one per thread.
  ///
  /// This is an interface class, so don't expose too much details.
  class EventLoop : noncopyable
  {
  public:
    typedef std::function<void()> Functor;

    EventLoop();
    ~EventLoop();

    void loop();

    void quit();

    void assertInLoopThread()
    {
      if (!isInLoopThread())
      {
        abortNotInLoopThread();
      }
    }

    bool isInLoopThread() const { return threadId_ == std::this_thread::get_id(); }

    static EventLoop* getEventLoopOfCurrentThread();

    void runInLoop(Functor cb);
    void queueInLoop(const Functor &cb);

    void runAfter(const m_timeval &delay, TimerCallback cb);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

  private:
    void abortNotInLoopThread();
    void wakeup();
    void handleRead();  // waked up
    void doPendingFunctors();

    typedef std::vector<Channel*> ChannelList;

    bool looping_; /* atomic */
    bool callingPendingFuncs_; /* atomic */
    std::thread::id threadId_;
    std::atomic<bool> quit_;
    std::unique_ptr<Poller> poller_;

    ChannelList activeChannels_;
    Channel* currentActiveChannel_;

    bool eventHandling_; /* atomic */
    MpscQueue<Functor> funcs_;
    std::unique_ptr<Timer> timer_;

    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;
  };

}  // namespace mutty

#endif  // MUTTY_NET_EVENTLOOP_H

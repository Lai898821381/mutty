#ifndef MUTTY_POLLER_H
#define MUTTY_POLLER_H

#include <map>
#include <vector>

#include "EventLoop.h"

namespace mutty {

  class Channel;

  class Poller : public noncopyable{
  public:
    typedef std::vector<Channel*> ChannelList;

    Poller(EventLoop* loop) : ownerLoop_(loop){};
    virtual ~Poller(){}

    virtual void poll(int timeoutMs, ChannelList* activeChannels) = 0;
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;
    virtual bool hasChannel(Channel* channel) const;

    static Poller* newDefaultPoller(EventLoop* loop);

    void assertInLoopThread() const
    {
      ownerLoop_->assertInLoopThread();
    }

  protected:
    using ChannelMap = std::map<int, Channel*>;
    ChannelMap channels_;

  private:
    EventLoop* ownerLoop_;
  };

}  // namespace mutty

#endif  // MUTTY_POLLER_H

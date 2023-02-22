#include "Poller.h"

#include "Channel.h"
#include "base/EPollPoller.h"

namespace mutty{
  bool Poller::hasChannel(Channel* channel) const
  {
    assertInLoopThread();
    ChannelMap::const_iterator it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
  }

  Poller* Poller::newDefaultPoller(EventLoop* loop){
    return new EPollPoller(loop);
  }
}
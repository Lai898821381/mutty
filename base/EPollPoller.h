#ifndef MUTTY_EPOLLPOLLER_H
#define MUTTY_EPOLLPOLLER_H

#include "../Poller.h"

#include <vector>

struct epoll_event;

namespace mutty{

  ///
  /// IO Multiplexing with epoll(4).
  ///
  class EPollPoller : public Poller {
    typedef std::vector<struct epoll_event> EventList;
  public:

    explicit EPollPoller(EventLoop* loop);
    ~EPollPoller() override;
    void poll(int timeoutMs, ChannelList* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;

  private:
    static const int kInitEventListSize = 16;
    int epollfd_;
    EventList events_;
    void update(int operation, Channel* channel);
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
  };

}  // namespace mutty
#endif  // MUTTY_EPOLLPOLLER_H

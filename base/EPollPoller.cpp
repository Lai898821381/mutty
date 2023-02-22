#include "EPollPoller.h"

#include "../Channel.h"

#include <assert.h>
#include <errno.h>
#include <iostream>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace mutty {

  // On Linux, the constants of poll(2) and epoll(4)
  // are expected to be the same.
  static_assert(EPOLLIN == POLLIN,        "epoll uses same flag values as poll");
  static_assert(EPOLLPRI == POLLPRI,      "epoll uses same flag values as poll");
  static_assert(EPOLLOUT == POLLOUT,      "epoll uses same flag values as poll");
  static_assert(EPOLLRDHUP == POLLRDHUP,  "epoll uses same flag values as poll");
  static_assert(EPOLLERR == POLLERR,      "epoll uses same flag values as poll");
  static_assert(EPOLLHUP == POLLHUP,      "epoll uses same flag values as poll");

  // #include <sys/epoll.h> 中定义的     case EPOLL_CTL_ADD EPOLL_CTL_DEL EPOLL_CTL_MOD:
  
  namespace
  {
  const int kNew = -1;
  const int kAdded = 1;
  const int kDeleted = 2;
  }

  EPollPoller::EPollPoller(EventLoop* loop)
    : Poller(loop),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize)
  {
    if (epollfd_ < 0)
    {
      std::cerr << "EPollPoller::EPollPoller";
      exit(1);
    }
  }

  EPollPoller::~EPollPoller()
  {
    ::close(epollfd_);
  }

  void EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
  {
    int numEvents = ::epoll_wait(epollfd_,
                                &*events_.begin(),
                                static_cast<int>(events_.size()),
                                timeoutMs);
    int savedErrno = errno;
    // Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
      fillActiveChannels(numEvents, activeChannels);
      if (static_cast<size_t>(numEvents) == events_.size())
      {
        events_.resize(events_.size()*2);
      }
    }
    else if (numEvents == 0)
    {
      // LOG_TRACE << "nothing happened";
    }
    else
    {
      // error happens, log uncommon ones
      if (savedErrno != EINTR)
      {
        errno = savedErrno;
        // LOG_SYSERR << "EPollPoller::poll()";
        std::cout << "EPollEpollPoller::poll()";
        exit(1);
      }
    }
  }

  void EPollPoller::fillActiveChannels(int numEvents,
                                      ChannelList* activeChannels) const
  {
    assert(static_cast<size_t>(numEvents) <= events_.size());
    for (int i = 0; i < numEvents; ++i)
    {
      Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
  #ifndef NDEBUG
      int fd = channel->fd();
      ChannelMap::const_iterator it = channels_.find(fd);
      assert(it != channels_.end());
      assert(it->second == channel);
  #endif
      channel->set_revents(events_[i].events);
      activeChannels->push_back(channel);
    }
  }

  void EPollPoller::updateChannel(Channel* channel)
  {
    Poller::assertInLoopThread();
    const int index = channel->index();
    if (index == kNew || index == kDeleted)
    {
      // a new one, add with EPOLL_CTL_ADD
      int fd = channel->fd();
      if (index == kNew)
      {
        assert(channels_.find(fd) == channels_.end());
        channels_[fd] = channel;
      }
      else // index == kDeleted
      {
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);
      }

      channel->set_index(kAdded);
      update(EPOLL_CTL_ADD, channel);
    }
    else
    {
      // update existing one with EPOLL_CTL_MOD/DEL
      int fd = channel->fd();
      (void)fd;
      assert(channels_.find(fd) != channels_.end());
      assert(channels_[fd] == channel);
      assert(index == kAdded);
      if (channel->isNoneEvent())
      {
        update(EPOLL_CTL_DEL, channel);
        channel->set_index(kDeleted);
      }
      else
      {
        update(EPOLL_CTL_MOD, channel);
      }
    }
  }

  void EPollPoller::removeChannel(Channel* channel)
  {
    Poller::assertInLoopThread();
    int fd = channel->fd();
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent());
    int index = channel->index();
    assert(index == kAdded || index == kDeleted);
    size_t n = channels_.erase(fd);
    (void)n;
    assert(n == 1);

    if (index == kAdded)
    {
      update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
  }

  void EPollPoller::update(int operation, Channel* channel)
  {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();
    event.data.ptr = channel;
    int fd = channel->fd();
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
      if (operation == EPOLL_CTL_DEL)
      {
        // LOG_SYSERR << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
      }
      else
      {
        // LOG_SYSFATAL << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
      }
    }
  }

} // namespace mutty

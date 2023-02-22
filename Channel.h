#ifndef MUTTY_CHANNEL_H
#define MUTTY_CHANNEL_H

#include "base/noncopyable.h"

#include <functional>
#include <memory>

namespace mutty {

  class EventLoop;

  class Channel : public noncopyable{
  public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    // ~Channel();

    void setReadCallback(EventCallback &cb)
    { readCallback_ = cb; }
    void setReadCallback(EventCallback &&cb)
    { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback &cb)
    { writeCallback_ = cb; }
    void setWriteCallback(EventCallback &&cb)
    { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback &cb)
    { closeCallback_ = cb; }
    void setCloseCallback(EventCallback &&cb)
    { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback &cb)
    { errorCallback_ = cb; }
    void setErrorCallback(EventCallback &&cb)
    { errorCallback_ = std::move(cb); }

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }
    bool isNoneEvent() const { return events_ == kNoneEvent; }

    void disableAll() { events_ = kNoneEvent; update(); }
    void remove();
    void handleEvent();
    EventLoop* ownerLoop() { return loop_; }

    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    void tie(const std::shared_ptr<void> &obj)
    {
        tie_ = obj;
        tied_ = true;
    }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

  private:
    void update();
    void handleEventWithGuard();

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;
    const int  fd_;
    int        events_;
    int        revents_; // it's the received event types of epoll or poll
    int        index_; // used by Poller.
    // bool eventHandling_;
    bool addedToLoop_{false};
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
    std::weak_ptr<void> tie_;
    bool tied_;  
  };
}  // namespace mutty

#endif  // MUTTY_CHANNEL_H

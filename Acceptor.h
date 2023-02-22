#ifndef MUTTY_ACCEPTOR_H
#define MUTTY_ACCEPTOR_H

#include <functional>

#include "Channel.h"
#include "Socket.h"

namespace mutty {

  class EventLoop;
  class InetAddress;

  class Acceptor : noncopyable
  {
  public:
    typedef std::function<void (int sockfd, const InetAddress&)> NewConnectionCallback;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCallback& cb)
    { newConnectionCallback_ = cb; }

    void listen();

  private:
    void handleRead();

    EventLoop* loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    int idleFd_;
  };

}  // namespace mutty

#endif  // MUTTY_ACCEPTOR_H

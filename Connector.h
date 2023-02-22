#ifndef MUTTY_CONNECTOR_H
#define MUTTY_CONNECTOR_H

#include "base/noncopyable.h"
#include "InetAddress.h"

#include <atomic>
#include <functional>
#include <memory>

namespace mutty {
  class Channel;
  class EventLoop;

  class Connector : public noncopyable,
                    public std::enable_shared_from_this<Connector>
  {
  public:
    using NewConnectionCallback = std::function<void (int sockfd)>;

    Connector(EventLoop* loop, const InetAddress& serverAddr);
    ~Connector();

    void setNewConnectionCallback(const NewConnectionCallback& cb)
    { newConnectionCallback_ = cb; }
    void setNewConnectionCallback(NewConnectionCallback &&cb)
    { newConnectionCallback_ = std::move(cb); }
    const InetAddress& serverAddress() const { return serverAddr_; }

    void start();
    // void restart(){}
    // void stop(){}


  private:
    NewConnectionCallback newConnectionCallback_;

    enum class Status { kDisconnected, kConnecting, kConnected };
    static constexpr int kMaxRetryDelayMs = 30*1000;
    static constexpr int kInitRetryDelayMs = 500;
    std::unique_ptr<Channel> channel_;
    EventLoop* loop_;
    InetAddress serverAddr_;

    std::atomic_bool connect_{false};
    std::atomic<Status> status_{Status::kDisconnected};

    int retryDelayMs_;

    void startInLoop();
    void connect();
    void connecting(int sockfd);
    int removeAndResetChannel();
    void handleWrite();
    void handleError();
    void retry(int sockfd);
    void resetChannel();
  };

}  // namespace mutty

#endif  // MUTTY_CONNECTOR_H

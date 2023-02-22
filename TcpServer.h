#ifndef MUTTY_TCPSERVER_H
#define MUTTY_TCPSERVER_H

#include <atomic>
#include <map>
#include <string>
#include "TcpConnection.h"
#include "EventLoopThreadPool.h"

namespace mutty
{

  class Acceptor;
  class EventLoop;
  class EventLoopThreadPool;

  class TcpServer : public noncopyable {
  public:

    TcpServer(EventLoop* loop,
              const InetAddress& listenAddr,
              const std::string& name,
              bool reUsePort = false);
    ~TcpServer();

    void start();

    void setIoLoopNum(size_t num)
    {
        threadPool_.reset(new EventLoopThreadPool(loop_, num));
        threadPool_->start();
    }

    const std::string& ipPort() const { return ipPort_; }
    const std::string& name() const { return name_; }
    EventLoop* getLoop() const { return loop_; }

    /// Set connection callback.
    /// Not thread safe.
    void setConnectionCallback(const ConnectionCallback& cb)
    { connectionCallback_ = cb; }

    /// Set message callback.
    /// Not thread safe.
    void setMessageCallback(const MessageCallback& cb)
    { messageCallback_ = cb; }

    /// Set write complete callback.
    /// Not thread safe.
    void setWriteCompleteCallback(const WriteCompleteCallback& cb)
    { writeCompleteCallback_ = cb; }

  private:
    /// Thread safe.
    void removeConnection(const TcpConnectionPtr& conn);
    /// Not thread safe, but in loop
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

    typedef std::map<std::string, TcpConnectionPtr> ConnectionMap;

    EventLoop* loop_;  // the acceptor loop
    std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor
    void newConnection(int sockfd, const InetAddress& peerAddr);
    const std::string name_;
    const std::string ipPort_;
    ConnectionMap connections_; // 该服务器建立的所有连接

    MessageCallback messageCallback_;
    ConnectionCallback connectionCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    // always in loop thread
    int nextConnId_; //下一个连接ID
    std::shared_ptr<EventLoopThreadPool> threadPool_;
  };

}  // namespace mutty

#endif  // MUTTY_TCPSERVER_H

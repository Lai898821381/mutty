#include <iostream>
#include <stdio.h>  // snprintf

#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "buffer/Buffer.h"
#include "EventLoopThreadPool.h"
#include "Socket.h"


using namespace std::placeholders;

namespace mutty{
  void defaultConnectionCallback(const TcpConnectionPtr& conn)
  {
  }

  void defaultMessageCallback(const TcpConnectionPtr&, buffer::Buffer* buf)
  {
    std::cout << "unhandled recv message [" << buf->readableBytes()
            << " bytes]";
    buf->retrieveAll();
  }

  TcpServer::TcpServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       const std::string& nameArg,
                       bool reUsePort)
    : loop_(loop),
      ipPort_(listenAddr.toIpPort()),
      acceptor_(new Acceptor(loop, listenAddr, reUsePort)),
      name_(nameArg),
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback),
      nextConnId_(1)
  {
    acceptor_->setNewConnectionCallback(
        std::bind(&TcpServer::newConnection, this, _1, _2));
  }

  TcpServer::~TcpServer()
  {
    loop_->assertInLoopThread();
    acceptor_.reset();
    for (auto& item : connections_)
    {
      TcpConnectionPtr conn(item.second);
      item.second.reset();
      conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
    }
    threadPool_.reset();
  }

  void TcpServer::start()
  {
    static std::once_flag flag;
    std::call_once(flag, [this] {
      threadPool_->start();

      loop_->runInLoop(
          std::bind(&Acceptor::listen, acceptor_.get()));
    });
  }
  // 非线程安全，只能在本线程调用
  // 新连接到达时Acceptor回调
  void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
  {
    std::cout << "new connection:fd=" << sockfd
              << " address=" << peerAddr.toIpPort();

    loop_->assertInLoopThread();
    EventLoop *ioLoop = nullptr;
    if (threadPool_ && threadPool_->size() > 0)
    {
        ioLoop = threadPool_->getNextLoop();
    }
    if (ioLoop == nullptr)
        ioLoop = loop_;
    char buf[64];
    snprintf(buf, sizeof(buf), "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    // std::cout << "TcpServer::newConnection [" << name_
    //         << "] - new connection [" << connName
    //         << "] from " << peerAddr.toIpPort();
    TcpConnectionPtr conn(
              new TcpConnection(ioLoop, connName, sockfd, InetAddress(Socket::getLocalAddr(sockfd)), peerAddr)
              );

    connections_[connName] = conn;
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
  }

  void TcpServer::removeConnection(const TcpConnectionPtr& conn)
  {
    std::cout << "connectionClosed";
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
    //在channel->handlevent后shared_ptr变成weak_ptr引用计数减一，故在此之前要添加一个引用计数
    static_cast<TcpConnection *>(conn.get())->connectDestroyed(); // ?
  }

  void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
  {
    loop_->assertInLoopThread();
    //因为channel中还有一个由weak_ptr升级的shared_ptr所以删除这个conn的引用计数也不会变成0
    size_t n = connections_.erase(conn->name());
    (void)n;
    assert(n == 1);
    EventLoop* ioLoop = conn->getLoop();
    // ioLoop->queueInLoop(
    //     std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

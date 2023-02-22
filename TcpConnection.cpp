
#include <iostream>
#include <errno.h>

#include "TcpConnection.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "buffer/Buffer.h"

namespace mutty{
  TcpConnection::TcpConnection(EventLoop* loop,
                              const string& nameArg,
                              int sockfd,
                              const InetAddress& localAddr,
                              const InetAddress& peerAddr)
    : loop_(loop),
      name_(nameArg),
      state_(kConnecting),
      reading_(true),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64*1024*1024)
  {
    inputBuffer_.swap(buffer::Buffer(16384));
    outputBuffer_.swap(buffer::Buffer(16384));
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this));
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this));
    socket_->setKeepAlive(true);
  }

  TcpConnection::~TcpConnection()
  {
    assert(state_ == kDisconnected);
  }

  void TcpConnection::send(const void* data, int len)
  {
    send(std::string(static_cast<const char*>(data), len));
  }

  void TcpConnection::send(const std::string& message)
  {
    if (state_ == kConnected)
    {
      if (loop_->isInLoopThread())
      {
        sendInLoop(message);
      }
      else
      {
        void (TcpConnection::*fp)(const std::string& message) = &TcpConnection::sendInLoop;
        loop_->runInLoop(
            std::bind(fp,
                      this,
                      message));
                      //std::forward<string>(message)));
      }
    }
  }

  void TcpConnection::send(buffer::Buffer* buf)
  {
    if (state_ == kConnected)
    {
      if (loop_->isInLoopThread())
      {
        sendInLoop(buf->peek(), buf->readableBytes());
        buf->retrieveAll();
      }
      else
      {
        void (TcpConnection::*fp)(const std::string& message) = &TcpConnection::sendInLoop;
        loop_->runInLoop(
            std::bind(fp,
                      this,     // FIXME
                      buf->retrieveAllAsString()));
                      //std::forward<string>(message)));
      }
    }
  }

  void TcpConnection::sendInLoop(const std::string& message)
  {
    sendInLoop(message.data(), message.size());
  }

  void TcpConnection::sendInLoop(const void* data, size_t len)
  {
    loop_->assertInLoopThread();
    //暂时不需要用到应用层缓冲区
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if (state_ == kDisconnected)
    {
      std::cout << "disconnected, give up writing";
      return;
    }
    // if no thing in output queue, try writing directly
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
      nwrote = ::write(channel_->fd(), data, len);
      if (nwrote >= 0)
      {
        remaining = len - nwrote;
        if (remaining == 0 && writeCompleteCallback_)
        {
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
      }
      else // nwrote < 0
      {
        nwrote = 0;
        if (errno != EWOULDBLOCK)
        {
          // LOG_SYSERR << "TcpConnection::sendInLoop";
          if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
          {
            faultError = true;
          }
        }
      }
    }

    assert(remaining <= len);
    if (!faultError && remaining > 0)
    {
      size_t oldLen = outputBuffer_.readableBytes();
      if (oldLen + remaining >= highWaterMark_
          && oldLen < highWaterMark_
          && highWaterMarkCallback_)
      {
        loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
      }
      outputBuffer_.writeBytes(static_cast<const char*>(data)+nwrote, remaining);
      if (!channel_->isWriting())
      {
        channel_->enableWriting(); // 关注POLLOUT事件
      }
    }
  }

  void TcpConnection::shutdownInLoop()
  {
    loop_->assertInLoopThread();
    if (!channel_->isWriting())
    {
      // we are not writing
      socket_->shutdownWrite();
    }
  }

  void TcpConnection::shutdown()
  {
    // FIXME: use compare and swap
    if (state_ == kConnected)
    {
      state_ = kDisconnecting;
      // FIXME: shared_from_this()?
      loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
  }

  void TcpConnection::forceClose()
  {
    // FIXME: use compare and swap
    if (state_ == kConnected || state_ == kDisconnecting)
    {
      state_ = kDisconnecting;
      loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
  }

  void TcpConnection::forceCloseInLoop()
  {
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting)
    {
      // as if we received 0 byte in handleRead();
      handleClose();
    }
  }

  const char* TcpConnection::stateToString() const
  {
    switch (state_)
    {
      case kDisconnected:
        return "kDisconnected";
      case kConnecting:
        return "kConnecting";
      case kConnected:
        return "kConnected";
      case kDisconnecting:
        return "kDisconnecting";
      default:
        return "unknown state";
    }
  }

  void TcpConnection::setTcpNoDelay(bool on)
  {
    socket_->setTcpNoDelay(on);
  }

  void TcpConnection::connectEstablished()
  {
    loop_->assertInLoopThread();
    assert(state_ == kConnecting);
    state_ = kConnected;
    channel_->tie(shared_from_this());
    channel_->enableReading();

    connectionCallback_(shared_from_this());
  }

  void TcpConnection::connectDestroyed()
  {
    loop_->assertInLoopThread();
    if (state_ == kConnected)
    {
      state_ = kDisconnected;
      channel_->disableAll();

      connectionCallback_(shared_from_this());
    }
    channel_->remove();
  }

  void TcpConnection::handleRead()
  {
    loop_->assertInLoopThread();
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
      messageCallback_(shared_from_this(), &inputBuffer_);
    }
    else if (n == 0)
    {
      handleClose();
    }
    else
    {
      errno = savedErrno;
      std::cout << "TcpConnection::handleRead";
      handleError();
    }
  }
  // 内核发送缓冲区有空间了，回调该函数
  void TcpConnection::handleWrite()
  {
    loop_->assertInLoopThread();
    if (channel_->isWriting())
    {
      ssize_t n = ::write(channel_->fd(),
                                outputBuffer_.peek(),
                                outputBuffer_.readableBytes());
      if (n > 0)
      {
        outputBuffer_.retrieve(n);
        if (outputBuffer_.readableBytes() == 0)
        {
          channel_->disableWriting();
          if (writeCompleteCallback_)
          {
            loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
          }
          if (state_ == kDisconnecting) // 发送缓冲区已清空并且连接状态是kDisconnecting, 要关闭连接
          {
            shutdownInLoop();
          }
        }
      }
      else
      {
        std::cout << "TcpConnection::handleWrite";
      }
    }
    else
    {
      std::cout << "Connection fd = " << channel_->fd()
                << " is down, no more writing";
    }
  }

  void TcpConnection::handleClose()
  {
    loop_->assertInLoopThread();
    std::cout << "fd = " << channel_->fd() << " state = " << stateToString();
    assert(state_ == kConnected || state_ == kDisconnecting);
    // we don't close fd, leave it to dtor, so we can find leaks easily.
    state_ = kDisconnected;
    channel_->disableAll();

    TcpConnectionPtr guardThis(shared_from_this());
    connectionCallback_(guardThis); // 可以不调用
    // must be the last line
    closeCallback_(guardThis); // 调用TcpServer::removeConnection
  }

  void TcpConnection::handleError()
  {
    int err = Socket::getSocketError(channel_->fd());
    std::cout << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << std::endl;
  }

}
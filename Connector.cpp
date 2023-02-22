#include "Connector.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"

#include <errno.h>

namespace mutty{
  Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
    : loop_(loop),
      serverAddr_(serverAddr),
      retryDelayMs_(kInitRetryDelayMs)
  {
  }

  // Connector::~Connector()
  // {
  //   assert(!channel_);
  // }

  void Connector::start()
  {
    connect_ = true;
    loop_->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
  }

  void Connector::startInLoop()
  {
    loop_->assertInLoopThread();
    assert(status_ == Status::kDisconnected);
    if (connect_)
    {
      connect();
    }
    else
    {
      // LOG_DEBUG << "do not connect";
    }
  }

  void Connector::connect()
  {
    int sockfd = Socket::createNonblockingOrDie(serverAddr_.family());
    errno = 0;
    int ret = Socket::connect(sockfd, serverAddr_);
    int savedErrno = (ret == 0) ? 0 : errno;
    switch (savedErrno)
    {
      case 0:
      case EINPROGRESS:
      case EINTR:
      case EISCONN:
        connecting(sockfd);
        break;

      case EAGAIN:
      case EADDRINUSE:
      case EADDRNOTAVAIL:
      case ECONNREFUSED:
      case ENETUNREACH:
        retry(sockfd);
        break;

      case EACCES:
      case EPERM:
      case EAFNOSUPPORT:
      case EALREADY:
      case EBADF:
      case EFAULT:
      case ENOTSOCK:
        // LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
        ::close(sockfd);
        break;

      default:
        // LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
        ::close(sockfd);
        // connectErrorCallback_();
        break;
    }
  }

  void Connector::connecting(int sockfd)
  {
    status_ = Status::kConnecting;
    assert(!channel_);
    channel_.reset(new Channel(loop_, sockfd));
    channel_->setWriteCallback(
        std::bind(&Connector::handleWrite, this)); // FIXME: unsafe
    channel_->setErrorCallback(
        std::bind(&Connector::handleError, this)); // FIXME: unsafe

    // channel_->tie(shared_from_this()); is not working,
    // as channel_ is not managed by shared_ptr
    channel_->enableWriting();
  }

  int Connector::removeAndResetChannel()
  {
    channel_->disableAll();
    channel_->remove();
    int sockfd = channel_->fd();
    // Can't reset channel_ here, because we are inside Channel::handleEvent
    loop_->queueInLoop(std::bind(&Connector::resetChannel, this)); // FIXME: unsafe
    return sockfd;
  }

  void Connector::resetChannel()
  {
    channel_.reset();
  }

  void Connector::handleWrite()
  {
    if (status_ == Status::kConnecting)
    {
      int sockfd = removeAndResetChannel();
      int err = Socket::getSocketError(sockfd);
      if (err)
      {
        // LOG_WARN << "Connector::handleWrite - SO_ERROR = "
        //          << err << " " << strerror_tl(err);
        retry(sockfd);
      }
      else if (Socket::isSelfConnect(sockfd))
      {
        // LOG_WARN << "Connector::handleWrite - Self connect";
        retry(sockfd);
      }
      else
      {
        status_ = Status::kConnected;
        if (connect_)
        {
          newConnectionCallback_(sockfd);
        }
        else
        {
          ::close(sockfd);
        }
      }
    }
    else
    {
      // what happened?
      assert(status_ == Status::kDisconnected);
    }
  }

  void Connector::handleError()
  {
    if (status_ == Status::kConnecting)
    {
      int sockfd = removeAndResetChannel();
      int err = Socket::getSocketError(sockfd);
      // LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
      retry(sockfd);
    }
  }

  void Connector::retry(int sockfd)
  {
    ::close(sockfd);
    status_ = Status::kDisconnected;
    if (connect_)
    {
      // LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
      //          << " in " << retryDelayMs_ << " milliseconds. ";
      loop_->runAfter(m_timeval(retryDelayMs_ * 1000),
                      std::bind(&Connector::startInLoop, shared_from_this()));
      retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
    }
    else
    {
      // LOG_DEBUG << "do not connect";
    }
  }

}
#include "Acceptor.h"

#include "EventLoop.h"
#include "InetAddress.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

namespace mutty{
  Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    : loop_(loop),
      acceptSocket_(Socket::createNonblockingOrDie(listenAddr.family())),
      acceptChannel_(loop, acceptSocket_.fd()),
      idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
  {
    assert(idleFd_ >= 0);
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(reuseport);
    acceptSocket_.bindAddress(listenAddr);
    acceptChannel_.setReadCallback(
        std::bind(&Acceptor::handleRead, this));
  }

  Acceptor::~Acceptor()
  {
    acceptChannel_.disableAll();
    acceptChannel_.remove();
    ::close(idleFd_);
  }

  void Acceptor::listen()
  {
    loop_->assertInLoopThread();
    acceptSocket_.listen();
    acceptChannel_.enableReading();
  }

  void Acceptor::handleRead()
  {
    loop_->assertInLoopThread();
    InetAddress peerAddr;
    //FIXME loop until no more
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
      // string hostport = peerAddr.toIpPort();
      // LOG_TRACE << "Accepts of " << hostport;
      if (newConnectionCallback_)
      {
        newConnectionCallback_(connfd, peerAddr);
      }
      else
      {
        if (::close(connfd) < 0)
        {
          std::cerr << "Socket closed failed";
          exit(1);
        }
      }
    }
    else
    {
      // LOG_SYSERR << "in Acceptor::handleRead";
      // Read the section named "The special problem of
      // accept()ing when you can't" in libev's doc.
      // By Marc Lehmann, author of libev.
      if (errno == EMFILE)
      {
        ::close(idleFd_);
        idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
        ::close(idleFd_);
        idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
      }
    }
  }
}
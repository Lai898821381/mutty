#include <iostream>
#include "Socket.h"

namespace mutty{
  // 自连接是指(sourceIP, sourcePort) = (destIP, destPort)
  // 自连接发生的原因:
  // 客户端在发起connect的时候，没有bind(2)
  // 客户端与服务器端在同一台机器，即sourceIP = destIP，
  // 服务器尚未开启，即服务器还没有在destPort端口上处于监听
  // 就有可能出现自连接，这样，服务器也无法启动
  bool Socket::isSelfConnect(int sockfd)
  {
    struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
    struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
    if (localaddr.sin6_family == AF_INET)
    {
      const struct sockaddr_in* laddr4 = reinterpret_cast<struct sockaddr_in*>(&localaddr);
      const struct sockaddr_in* raddr4 = reinterpret_cast<struct sockaddr_in*>(&peeraddr);
      return laddr4->sin_port == raddr4->sin_port
          && laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
    }
    else if (localaddr.sin6_family == AF_INET6)
    {
      return localaddr.sin6_port == peeraddr.sin6_port
          && memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr, sizeof localaddr.sin6_addr) == 0;
    }
    else
    {
      return false;
    }
  }

  void Socket::bindAddress(const InetAddress& addr)
  {
    int ret = ::bind(sockfd_, addr.getSockAddr(), static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
    if (ret < 0)
    {
      std::cerr << "sockets::bindOrDie";
      exit(1);
    }
  }

  void Socket::listen()
  {
    int ret = ::listen(sockfd_, SOMAXCONN);
    if (ret < 0)
    {
      // std::cerr << "sockets::listenOrDie";
      exit(1);
    }
  }

  int Socket::accept(InetAddress* peeraddr)
  {
    struct sockaddr_in6 addr6;
    memset(&addr6, 0, sizeof(addr6));
    socklen_t addrlen = sizeof(addr6);
    int connfd = ::accept4(sockfd_, (struct sockaddr *)&addr6,
                          &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        peeraddr->setSockAddrInet6(addr6);
    }
    return connfd;
  }

  void Socket::shutdownWrite()
  {
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
      std::cerr << "sockets::shutdownWrite" << std::endl;
      exit(1);
    }
  }

  int Socket::read(char *buffer, uint64_t len)
  {
      return ::read(sockfd_, buffer, len);
  }

  struct sockaddr_in6 Socket::getLocalAddr(int sockfd)
  {
    struct sockaddr_in6 localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
    if (::getsockname(sockfd,
                      static_cast<struct sockaddr *>((void *)(&localaddr)), 
                      &addrlen) < 0)
    {
      std::cerr << "sockets::getLocalAddr";
      exit(1);
    }
    return localaddr;
  }

  struct sockaddr_in6 Socket::getPeerAddr(int sockfd)
  {
    struct sockaddr_in6 peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
    if (::getpeername(sockfd,
                      static_cast<struct sockaddr *>((void *)(&peeraddr)),
                      &addrlen) < 0)
    {
      std::cerr << "sockets::getPeerAddr";
      exit(1);
    }
    return peeraddr;
  }

  void Socket::setTcpNoDelay(bool on)
  {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY,
                &optval, static_cast<socklen_t>(sizeof optval));
    // FIXME CHECK
  }

  void Socket::setReuseAddr(bool on)
  {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,
                &optval, static_cast<socklen_t>(sizeof optval));
    // FIXME CHECK
  }

  void Socket::setReusePort(bool on)
  {
  #ifdef SO_REUSEPORT
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT,
                          &optval, static_cast<socklen_t>(sizeof optval));
    if (ret < 0 && on)
    {
      // LOG_SYSERR << "SO_REUSEPORT failed.";
    }
  #else
    if (on)
    {
      std::cerr << "SO_REUSEPORT is not supported.";
      exit(1);
    }
  #endif
  }

  void Socket::setKeepAlive(bool on)
  {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE,
                &optval, static_cast<socklen_t>(sizeof optval));
    // FIXME CHECK
  }

  Socket::~Socket()
  {
    std::cout << "Socket deconstructed:" << sockfd_;
    if (sockfd_ >= 0)
        close(sockfd_);
  }

  bool Socket::getTcpInfo(struct tcp_info* tcpi) const
  {
    socklen_t len = sizeof(*tcpi);
    memset(tcpi, 0, len);
    return ::getsockopt(sockfd_, SOL_TCP, TCP_INFO, tcpi, &len) == 0;
  }

  bool Socket::getTcpInfoString(char* buf, int len) const
  {
    struct tcp_info tcpi;
    bool ok = getTcpInfo(&tcpi);
    if (ok)
    {
      snprintf(buf, len, "unrecovered=%u "
              "rto=%u ato=%u snd_mss=%u rcv_mss=%u "
              "lost=%u retrans=%u rtt=%u rttvar=%u "
              "sshthresh=%u cwnd=%u total_retrans=%u",
              tcpi.tcpi_retransmits,  // Number of unrecovered [RTO] timeouts
              tcpi.tcpi_rto,          // Retransmit timeout in usec
              tcpi.tcpi_ato,          // Predicted tick of soft clock in usec
              tcpi.tcpi_snd_mss,
              tcpi.tcpi_rcv_mss,
              tcpi.tcpi_lost,         // Lost packets
              tcpi.tcpi_retrans,      // Retransmitted packets out
              tcpi.tcpi_rtt,          // Smoothed round trip time in usec
              tcpi.tcpi_rttvar,       // Medium deviation
              tcpi.tcpi_snd_ssthresh,
              tcpi.tcpi_snd_cwnd,
              tcpi.tcpi_total_retrans);  // Total retransmits for entire connection
    }
    return ok;
  }
}
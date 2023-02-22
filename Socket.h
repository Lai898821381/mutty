#ifndef MUTTY_SOCKET_H
#define MUTTY_SOCKET_H

#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "InetAddress.h"
#include "base/noncopyable.h"

namespace mutty{
  class InetAddress;
  class Socket : public noncopyable{
  public:

    static void setNonBlockAndCloseOnExec(int sockfd)
    {
      // non-block fcntl: POSIX设置文件描述符属性
      // F_SETFD仅更改该文件描述符的信息，而F_SETFL则是更改与该文件相关的所有描述符
      // O_CLOEXEC：在进程执行exec系统调用时关闭此打开的文件描述符。防止父进程泄露打开的文件给子进程，即便子进程没有相应权限
      int flags = ::fcntl(sockfd, F_GETFL, 0);
      flags |= O_NONBLOCK;
      int ret = ::fcntl(sockfd, F_SETFL, flags);
      // FIXME check

      // close-on-exec
      flags = ::fcntl(sockfd, F_GETFD, 0);
      flags |= FD_CLOEXEC;
      ret = ::fcntl(sockfd, F_SETFD, flags);
      // FIXME check

      (void)ret;
    }

    static int createNonblockingOrDie(sa_family_t family)
    {
      int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
      if (sockfd < 0)
      {
        // LOG_SYSFATAL << "sockets::createNonblockingOrDie";
        exit(1);
      }
      return sockfd;
    }

    //获取并清除socket的错误状态 
    static int getSocketError(int sockfd)
    {
      //fcntl系统调用是控制文件描述符属性的通用的POSIX方法
      //getsockopt以及setsockopt是专门用来读取和设置socket文件描述符属性的方法
      int optval;
      socklen_t optlen = static_cast<socklen_t>(sizeof(optval));

      if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
      {
        return errno;
      }
      else
      {
        return optval;
      }
    }

    static int connect(int sockfd, const InetAddress &addr)
    {
      // return ::connect(sockfd, addr.getSockAddr(), static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
      // return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
      if (addr.isIpV6())
          return ::connect(sockfd, addr.getSockAddr(), static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
      else
          return ::connect(sockfd, addr.getSockAddr(), static_cast<socklen_t>(sizeof(struct sockaddr_in)));
    }
  
    // RALLhandle,封装了socket文件描述符的生命期。
    explicit Socket(int sockfd)
      : sockfd_(sockfd)
    { }
    ~Socket();
    /// abort if address in use
    void bindAddress(const InetAddress& localaddr);
    /// abort if address in use
    void listen();
    int accept(InetAddress* peeraddr);
    void shutdownWrite();
    int read(char *buffer, uint64_t len);
    int fd() const { return sockfd_; }
    // return true if success.
    bool getTcpInfo(struct tcp_info*) const;
    bool getTcpInfoString(char* buf, int len) const;
    
    static struct sockaddr_in6 getLocalAddr(int sockfd);//获取套接字本地协议地址
    static struct sockaddr_in6 getPeerAddr(int sockfd);//获取与某个套接字相关的外部协议地址

    ///
    /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
    ///
    void setTcpNoDelay(bool on);

    ///
    /// Enable/disable SO_REUSEADDR
    ///
    void setReuseAddr(bool on);

    ///
    /// Enable/disable SO_REUSEPORT
    ///
    void setReusePort(bool on);

    ///
    /// Enable/disable SO_KEEPALIVE
    ///
    void setKeepAlive(bool on);
    static bool isSelfConnect(int sockfd);

  private:
    const int sockfd_;
  };

  }  // namespace mutty

#endif  // MUTTY_SOCKET_H

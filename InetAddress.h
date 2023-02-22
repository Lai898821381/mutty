#ifndef MUTTY_INETADDRESS_H
#define MUTTY_INETADDRESS_H

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstring>
#include <string>
#include <assert.h>
#include <netdb.h>

namespace mutty {
  // 对网络地址的相关封装，包括初始化网络地址结构，设置/获取网络地址数据等等。这里包含IPv4和IPv6
  // 对struct sockaddr_in的简单封装，能自动转换字节序。
  class InetAddress {
  public:
    // 仅仅指定port，不指定ip，则ip为INADDR_ANY（即0.0.0.0）
    explicit InetAddress(uint16_t port = 0, bool loopbackOnly = false, bool ipv6 = false);

    InetAddress(const std::string &ip, uint16_t port, bool ipv6 = false);

    explicit InetAddress(const struct sockaddr_in& addr)
      : addr_(addr)
    { }

    explicit InetAddress(const struct sockaddr_in6& addr)
      : addr6_(addr), isIpV6_(true)
    { }

    inline sa_family_t family() const { return addr_.sin_family; }
    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t toPort() const;

    inline bool isIpV6() const
    {
        return isIpV6_;
    }

    const struct sockaddr* getSockAddr() const 
    { 
      return static_cast<const struct sockaddr *>((void *)(&addr6_));
    }
    void setSockAddrInet6(const struct sockaddr_in6& addr6) {
      addr6_ = addr6;
      isIpV6_ = (addr6_.sin6_family == AF_INET6);
    }

    uint16_t portNetEndian() const { return addr_.sin_port; }

    // resolve hostname to IP address, not changing port or sin_family
    // return true on success.
    // thread safe
    //由域名获取地址
    static bool resolve(std::string& hostname, InetAddress* result);
    // static std::vector<InetAddress> resolveAll(const char* hostname, uint16_t port = 0);

  private:
    union
    {
      struct sockaddr_in addr_;
      struct sockaddr_in6 addr6_;
    };
    bool isIpV6_{false};
  };

}  // namespace mutty

#endif  // MUTTY_INETADDRESS_H

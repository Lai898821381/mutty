#include <iostream>
#include "InetAddress.h"

//     /* Structure describing an Internet socket address.  */
//     sa_data把目标地址和端口信息混在一起 #include <sys/socket.h>
//     struct sockaddr  
//     {
//         __SOCKADDR_COMMON (sa_);	/* Common data: address family and length.  */
//         char sa_data[14];		/* Address data.  */
//     };
//     #include<netinet/in.h>或#include <arpa/inet.h>
//     typedef unsigned short int sa_family_t;
//     struct sockaddr_in {
//         sa_family_t    sin_family; /* address family: AF_INET */
//         uint16_t       sin_port;   /* port in network byte order */
//         struct in_addr sin_addr;   /* internet address */
//     };

//     /* Internet address. */
//     typedef uint32_t in_addr_t;
//     struct in_addr {
//         in_addr_t       s_addr;     /* address in network byte order */
//     };

//     struct sockaddr_in6 {
//         sa_family_t     sin6_family;   /* address family: AF_INET6 */
//         uint16_t        sin6_port;     /* port in network byte order */
//         uint32_t        sin6_flowinfo; /* IPv6 flow information */
//         struct in6_addr sin6_addr;     /* IPv6 address */
//         uint32_t        sin6_scope_id; /* IPv6 scope-id */
//     };

namespace mutty {
  // deleted because of add isIpV6_
  // static_assert(sizeof(InetAddress) == sizeof(struct sockaddr_in6),
  //               "InetAddress is same size as sockaddr_in6");
  // size_t offsetof(type, member);
  // 该宏返回的是一个结构成员相对于结构开头的字节偏移量。type为结构体，member为结构体中的某个成员。
  static_assert(offsetof(sockaddr_in, sin_family) == 0, "sin_family offset 0");
  static_assert(offsetof(sockaddr_in6, sin6_family) == 0, "sin6_family offset 0");
  static_assert(offsetof(sockaddr_in, sin_port) == 2, "sin_port offset 2");
  static_assert(offsetof(sockaddr_in6, sin6_port) == 2, "sin6_port offset 2");

  static const in_addr_t kInaddrAny = INADDR_ANY;
  static const in_addr_t kInaddrLoopback = INADDR_LOOPBACK;

  InetAddress::InetAddress(uint16_t port, bool loopbackOnly, bool ipv6)
  : isIpV6_(ipv6)
  {
    static_assert(offsetof(InetAddress, addr6_) == 0, "addr6_ offset 0");
    static_assert(offsetof(InetAddress, addr_) == 0, "addr_ offset 0");
    if (ipv6)
    {
      memset(&addr6_, 0, sizeof(addr6_));
      addr6_.sin6_family = AF_INET6;
      in6_addr ip = loopbackOnly ? in6addr_loopback : in6addr_any;
      addr6_.sin6_addr = ip;
      addr6_.sin6_port = htons(port);
    }
    else
    {
      memset(&addr_, 0, sizeof(addr_));
      addr_.sin_family = AF_INET;
      in_addr_t ip = loopbackOnly ? kInaddrLoopback : kInaddrAny;
      addr_.sin_addr.s_addr = htonl(ip);
      addr_.sin_port = htons(port);
    }
  }

  InetAddress::InetAddress(const std::string &ip, uint16_t port, bool ipv6)
  : isIpV6_(ipv6)
  {
    if (ipv6)
    {
      memset(&addr6_, 0, sizeof(addr6_));
      addr6_.sin6_family = AF_INET6;
      addr6_.sin6_port = htons(port);
      if (::inet_pton(AF_INET6, ip.c_str(), &addr6_.sin6_addr) <= 0)
      {
        std::cout << "sockets::ipv6Netendian transfer failed"<<std::endl;
        exit(1);
      }
    }
    else
    {
      memset(&addr_, 0, sizeof(addr_));
      addr_.sin_family = AF_INET;
      addr_.sin_port = htons(port);
      if (::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr) <= 0)
      {
        std::cout << "sockets::ipv4Netendian transfer failed"<<std::endl;
        exit(1);
      }
    }
  }

  std::string InetAddress::toIpPort() const
  {
    char buf[64] = "";
    snprintf(buf, sizeof(buf), ":%u", toPort());
    return toIp() + static_cast<std::string>(buf);
  }

  std::string InetAddress::toIp() const
  {
    /*
    int inet_pton(int family, const char *strptr, void *addrptr);     
    //将点分十进制的ip地址转化为用于网络传输的数值格式
    // 返回值：若成功则为1，若输入不是有效的表达式则为0，若出错则为-1
    const char * inet_ntop(int family, const void *addrptr, char *strptr, size_t len);     
    //将数值格式转化为点分十进制的ip地址格式
    */  
    char buf[64] = "";
    if (addr_.sin_family == AF_INET)
    {
      // inet_ntop: 网络字节序转换成点分十进制字符串
      ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    }
    else if (addr_.sin_family == AF_INET6)
    {
      ::inet_ntop(AF_INET6, &addr6_.sin6_addr, buf, sizeof(buf));
    }
    return buf;
  }

  uint16_t InetAddress::toPort() const
  {
    return ntohs(portNetEndian());
  }

  static __thread char t_resolveBuffer[64 * 1024];

  bool InetAddress::resolve(std::string& hostname, InetAddress* out)
  {
    assert(out != NULL);
    struct hostent hent;
    struct hostent* he = NULL;
    int herrno = 0;
    memset(&hent, 0, sizeof(hent));
    int ret = gethostbyname_r(hostname.c_str(), &hent, t_resolveBuffer, sizeof t_resolveBuffer, &he, &herrno);
    if (ret == 0 && he != NULL)
    {
      assert(he->h_addrtype == AF_INET && he->h_length == sizeof(uint32_t));
      out->addr_.sin_addr = *reinterpret_cast<struct in_addr*>(he->h_addr);
      return true;
    }
    else
    {
      if (ret)
      {
        std::cerr << "InetAddress::resolve";
        exit(1);
      }
      return false;
    }
  }
}
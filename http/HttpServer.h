#ifndef MUTTY_HTTP_HTTPSERVER_H
#define MUTTY_HTTP_HTTPSERVER_H

#include "../TcpServer.h"
#include "../base/noncopyable.h"
#include "../base/any.h"

using namespace buffer;
using namespace base;

namespace mutty {
  namespace http{
    class HttpRequest;
    class HttpResponse;

    class HttpServer : noncopyable {
    public:
      typedef std::function<void (const HttpRequest&,
                                  HttpResponse*)> HttpCallback;

      HttpServer(EventLoop* loop,
                const InetAddress& listenAddr,
                const std::string& name,
                bool reusePort = false);

      mutty::EventLoop* getLoop() const { return server_.getLoop(); }

      /// Not thread safe, callback be registered before calling start().
      void setHttpCallback(const HttpCallback& cb)
      {
        httpCallback_ = cb;
      }

      void setThreadNum(int numThreads)
      {
        server_.setIoLoopNum(numThreads);
      }

      void start();

    private:
      void onConnection(const TcpConnectionPtr& conn);
      void onMessage(const TcpConnectionPtr& conn,
                    Buffer* buf);
      void onRequest(const TcpConnectionPtr&, const HttpRequest&);

      TcpServer server_;
      HttpCallback httpCallback_;
    };
  } // namespace http
}  // namespace mutty

#endif  // MUTTY_HTTP_HTTPSERVER_H

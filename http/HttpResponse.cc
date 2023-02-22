#include <stdio.h>
#include "HttpResponse.h"

namespace mutty {
  namespace http {
    void HttpResponse::appendToBuffer(Buffer* output) const {
      char buf[32];
      snprintf(buf, sizeof buf, "HTTP/1.1 %d ", statusCode_);
      output->writeBytes(buf)->writeBytes(statusMessage_)->writeBytes("\r\n");

      if (closeConnection_) {
        output->writeBytes("Connection: close\r\n");
      }
      else {
        snprintf(buf, sizeof buf, "Content-Length: %zd\r\n", body_.size());
        output->writeBytes(buf);
        output->writeBytes("Connection: Keep-Alive\r\n");
      }

      for (const auto& header : headers_) {
        output->writeBytes(header.first);
        output->writeBytes(": ");
        output->writeBytes(header.second);
        output->writeBytes("\r\n");
      }

      output->writeBytes("\r\n");
      output->writeBytes(body_);
    }
  }
}
#ifndef MUTTY_CALLBACKS_H
#define MUTTY_CALLBACKS_H

#include <functional>
#include "buffer/Buffer.h"

namespace mutty
{

// class Buffer;
using TimerCallback = std::function<void()>;

class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

using MessageCallback = std::function<void (const TcpConnectionPtr&, buffer::Buffer*)>;
using ConnectionCallback = std::function<void (const TcpConnectionPtr&)>;
using CloseCallback = std::function<void (const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void (const TcpConnectionPtr&)>;
using HighWaterMarkCallback = std::function<void (const TcpConnectionPtr&, size_t)>;

}  // namespace mutty

#endif  // MUTTY_CALLBACKS_H

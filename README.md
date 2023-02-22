<h1 align='center'>mutty</h1>

### 介绍

参考muduo实现的基于epoll反应堆的c++高性能网络通信库，可以接收HTTP请求、发送HTTP响应，使用**内存池**处理缓冲区读写，极大了增强epoll反应堆对发送数据大小的动态范围，减少了内存碎片。同时利用**时间轮定时器**处理定时任务，实现了高并发定时。**定时器可以作为独立组件，即插即用，减少系统耦合**。而muduo是将定时事件交由EventLoop线程处理，每个EventLoop线程拥有一个单独的定时器。

### 组件

#### buffer

* 参考netty的jemalloc4内存池构建，线程安全，多线程环境下性能显著优于单线程。[jemalloc4内存池](./buffer/README.md)
* 包含writeIndex和readerIndex，用来读写缓冲区。

#### timer

* 高性能时间轮定时器，超10万+的并发，支持微秒级别的任务定时操作。[timer](./timer/README.md)

#### LongAdder

* 高性能long类型原子计数类，通过分散热点资源的方式减少线程间的资源争用，可实现多线程下高性能的long计数，测试中具有比atomic<long>高约一倍的性能。[LongAdder](./buffer/LongAdder/README.md)

#### http

* HTTP服务器，用于接收HTTP请求和发送HTTP响应。

### 结果

在8核i7-4790测试环境下，利用wrk测试不同大小返回响应下，mutty与muduo的qps
测试指令为`wrk -t12 -c400 -d10s http://127.0.0.1:7000`

|        | 1024         | 4096         | 16384        | 40958        | 57342        | 65536        | 81920        |
| ------ | ------------ | ------------ | ------------ | ------------ | ------------ | ------------ | ------------ |
| mutty  | 57326.57     | 57412.20     | 55343.91     | 50751.91     | **49149.86** | **46143.84** | **37082.47** |
| muduo  | **63856.50** | **63380.92** | **61179.20** | **56835.15** | 44985.58     | 39376.64     |  34319.87    |

结果如上所示，最上一行是返回响应的大小，分别为1024B，4096B，16384B，……。粗体为性能较好的一方。在返回响应大小57342时，mutty的qps超过muduo；在40958时，muduo的qps显著下降，而mutty基本稳定。可见mutty对响应大小的动态范围更好。

#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>

#include "PooledByteBufAllocator.h"

namespace buffer{
  Buffer::Buffer()
  :m_internalByteBuf(nullptr)
  {
  }
  
  Buffer::Buffer(int size)
  {
    assert(size >= 0);
    m_internalByteBuf = PooledByteBufAllocator::ALLOCATOR()->buffer(size);
  }

  Buffer::~Buffer()
  {
    if(m_internalByteBuf != nullptr)
      m_internalByteBuf->deallocate();
  }

  ssize_t Buffer::readFd(int fd, int* savedErrno)
  {
    // saved an ioctl()/FIONREAD call to tell how much to read
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + m_internalByteBuf->m_writerIndex;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    // when there is enough space in this buffer, don't read into extrabuf.
    // when extrabuf is used, we read 128k-1 bytes at most.
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
      *savedErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
      m_internalByteBuf->m_writerIndex += n;
    }
    else
    {
      m_internalByteBuf->m_writerIndex = capacity();
      writeBytes(extrabuf, n - writable);
    }
    return n;
  }
}


/*
// 将三个独立的字符串一次写入终端
#include <sys/uio.h>
int main(int argc,char **argv)
{
    char part1[] = "This is iov";
    char part2[] = " and ";
    char part3[] = " writev test";
    struct iovec iov[3];
    iov[0].iov_base = part1;
    iov[0].iov_len = strlen(part1);
    iov[1].iov_base = part2;
    iov[1].iov_len = strlen(part2);
    iov[2].iov_base = part3;
    iov[2].iov_len = strlen(part3);
    writev(1,iov,3);
    return 0;
}
*/
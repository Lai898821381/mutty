#ifndef MUTTY_BUFFER_H
#define MUTTY_BUFFER_H

#include <algorithm>
#include <vector>
#include <string>
#include <assert.h>
#include <string.h>
#include <iostream>
#include "PooledByteBuf.h"

//#include <unistd.h>  // ssize_t

namespace buffer
{

class Buffer{
  static constexpr char kCRLF[] = "\r\n";
 public:
  static const size_t kCheapPrepend = 8;
  static const size_t kInitialSize = 1024;

  Buffer();
  explicit Buffer(int size);
  ~Buffer();

  inline const int readableBytes() const
  { return m_internalByteBuf->readableBytes(); }

  inline const int writableBytes() const
  { return m_internalByteBuf->writableBytes(); }

  void swap(Buffer&& rhs)
  {
    std::swap(m_internalByteBuf, rhs.m_internalByteBuf);
  }

  inline const char* peek() const { return m_internalByteBuf->peek(); }
  inline char* begin() const { return m_internalByteBuf->tmpBuf(); }
  inline const int capacity() const { return m_internalByteBuf->capacity(); }

  // retrieve returns void, to prevent
  // string str(retrieve(readableBytes()), readableBytes());
  // the evaluation of two functions are unspecified
  void retrieve(size_t len) { m_internalByteBuf->retrieve(len); }
  void retrieveAll() { m_internalByteBuf->retrieveAll(); }
  std::string retrieveAllAsString() { return m_internalByteBuf->retrieveAllAsString(); }
  std::string retrieveAsString(size_t len) { return m_internalByteBuf->retrieveAsString(len); }

  // append
  Buffer * writeBytes(const char* data, int length) {
      m_internalByteBuf->writeBytes(data, length);
      return this;
  }

  Buffer * writeBytes(const std::string& str ) {
      m_internalByteBuf->writeBytes(str.data(), str.size());
      return this;
  }

  // void hasWritten(size_t len)
  // {
  //   assert(len <= writableBytes());
  //   m_internalByteBuf->m_writerIndex += len;
  // }

  // void unwrite(size_t len)
  // {
  //   assert(len <= readableBytes());
  //   m_internalByteBuf->m_writerIndex -= len;
  // }

  inline char* beginWrite() { return begin() + m_internalByteBuf->m_writerIndex; }
  inline const char* beginWrite() const { return begin() + m_internalByteBuf->m_writerIndex; }

  const char* findCRLF() const
  {
    // FIXME: replace with memmem()?
    const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF+2);
    return crlf == beginWrite() ? NULL : crlf;
  }

  const char* findCRLF(const char* start) const
  {
    assert(peek() <= start);
    assert(start <= beginWrite());
    // FIXME: replace with memmem()?
    const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF+2);
    return crlf == beginWrite() ? NULL : crlf;
  }

  void retrieveUntil(const char* end)
  {
      assert(peek() <= end);
      assert(end <= beginWrite());
      retrieve(end - peek());
  }

  /// Read data directly into buffer.
  ///
  /// It may implement with readv(2)
  /// @return result of read(2), @c errno is saved
  ssize_t readFd(int fd, int* savedErrno);

 private:
  PooledByteBuf* m_internalByteBuf;
  // std::vector<char> buffer_;
};

}  // namespace buffer

#endif  // MUTTY_BUFFER_H

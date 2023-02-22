#include "PooledByteBuf.h"
#include "PooledByteBufAllocator.h"
#include "Recycler.h"

namespace buffer{
    PooledByteBuf::PooledByteBuf(int maxCapacity)
    : m_allocator(nullptr), m_maxCapacity(maxCapacity), m_chunk(nullptr), m_handle(0), m_memory(nullptr), m_offset(0), 
      m_length(0), m_maxLength(0), m_cache(nullptr), m_tmpBuf(nullptr), m_readerIndex(0), m_writerIndex(0)
    {
        assert(maxCapacity >= 0);
    }

    // 调用完后说明，chunk中从偏移量offset开始总长度为length的内存被这个PooledByteBuf占用了
    void PooledByteBuf::init0(PoolChunk* chunk, const char* buffer, long handle, int offset, int length, int maxLength, PoolThreadCache* cache){
        assert(handle >= 0);
        assert(chunk != nullptr);

        m_chunk = chunk;
        m_memory = chunk->m_memory;
        m_allocator = chunk->m_arena->m_parent;
        m_cache = cache;
        m_handle = handle;
        m_offset = offset;
        m_length = length;
        m_maxLength = maxLength;
        if(buffer == nullptr)
            buffer = m_memory + m_offset;
        m_tmpBuf = const_cast<char*>(buffer);
    }
        
    // PooledByteBuf
    void PooledByteBuf::deallocate(){
        if (m_handle >= 0) {
            // 清除对象相关属性
            long handle = m_handle;
            m_handle = -1;
            m_memory = nullptr;
            // 释放PoolChunk中申请的内存空间 注意是handle，而不是m_handle
            m_chunk->m_arena->free(m_chunk, m_tmpBuf, handle, m_maxLength, m_cache);
            m_tmpBuf = nullptr;
            m_chunk = nullptr;
            // 回收这个PooledByteBuf
            recycle();
        }
    }

    void PooledByteBuf::recycle(){
        m_recycler->recycle(this);
    }

    char* PooledByteBuf::internalBuffer(){
        if (m_tmpBuf == nullptr) {
            m_tmpBuf = newInternalBuffer(m_memory);
        } else {
            // m_tmpBuf.clear();
        }
        return m_tmpBuf;
    }

    char* PooledByteBuf::_internalBuffer(int index, int length, bool duplicate) {
        index = idx(index);
        // char* buffer = duplicate ? newInternalBuffer(m_memory) : internalBuffer();
        // buffer.limit(index + length).position(index);
        // buffer->limit(index + length).position(index);
        m_tmpBuf = m_memory + index;
        return m_tmpBuf;
    }

    char* PooledByteBuf::duplicateInternalBuffer(int index, int length){
        // checkIndex(index, length);
        // ensureAccessible();
        // checkIndex0(index, fieldLength);
        // checkRangeBounds("index", index, fieldLength, capacity());
        // !isOutOfBounds(index, fieldLength, capacity);
        assert(index + length <= m_length);
        return _internalBuffer(index, length, true);
    }

    char* PooledByteBuf::internalBuffer(int index, int length){
        // checkIndex(index, length);
        assert(index + length <= m_length);
        return _internalBuffer(index, length, false);
    }

    void PooledByteBuf::reuse(int maxCapacity){
        setMaxCapacity(maxCapacity);
        setIndex0(0, 0);
    }

    void PooledByteBuf::setIndex0(int readerIndex, int writerIndex) {
        m_readerIndex = readerIndex;
        m_writerIndex = writerIndex;
    }

    void PooledByteBuf::setMaxCapacity(int maxCapacity) {
        m_maxCapacity = maxCapacity;
    }

    PooledByteBuf* PooledByteBuf::newInstance(int maxCapacity){
        PooledByteBuf* buf = m_recycler->get();
        buf->reuse(maxCapacity);
        return buf;
    }

    PooledByteBuf* PooledByteBufRecycle::newObject(){
        return new PooledByteBuf(0);
    }

    Recycler<PooledByteBuf> *PooledByteBuf::m_recycler = new PooledByteBufRecycle();

    char* PooledByteBuf::newInternalBuffer(const char* memory){
        // Direct return memory.duplicate();
        // Heap return ByteBuffer.wrap(memory);
        return m_memory;
    }

    void PooledByteBuf::checkIndexBounds(int readerIndex, int writerIndex, int capacity){
        if (readerIndex < 0 || readerIndex > writerIndex || writerIndex > capacity) {
            throw std::exception();
        }
    }

    int PooledByteBuf::maxWritableBytes(){
        return maxCapacity() - m_writerIndex;
    }

    void PooledByteBuf::retrieve(size_t len){
        assert(len <= readableBytes());
        if (len < readableBytes())
        {
            m_readerIndex += len;
        }
        else
        {
            retrieveAll();
        }
    }

    std::string PooledByteBuf::retrieveAllAsString(){
        return retrieveAsString(readableBytes());
    }

    std::string PooledByteBuf::retrieveAsString(size_t len){
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    void PooledByteBuf::retrieveAll(){
        m_readerIndex = 0;
        m_writerIndex = 0;
    }

    // void PooledByteBuf::checkReadableBytes(int minimumReadableBytes){
    //     assert(minimumReadableBytes >= 0);
    //     assert(readableBytes() >= minimumReadableBytes);
    //     // if (readableBytes() < minimumReadableBytes){
    //     //     throw std::exception();
    //     // }
    // }

    PooledByteBuf* PooledByteBuf::writeBytes(const char* src, int srcIndex, int length){
        // 确保可写
        ensureWritable(length);
        // 写入数据，由于不同子类有不同的复制操作，所以由子类实现
        setBytes(m_writerIndex, src, srcIndex, length);
        m_writerIndex += length;
        return this;
    }

    PooledByteBuf* PooledByteBuf::writeBytes(const char* src, int length){
        writeBytes(src, 0, length);
        return this;
    }

    // PooledByteBuf* PooledByteBuf::writeBytes(PooledByteBuf* src){
    //     writeBytes(src, src->readableBytes());
    //     return this;
    // }

    // PooledByteBuf* PooledByteBuf::writeBytes(PooledByteBuf* src, int length){
    //     checkReadableBounds(src, length);
    //     writeBytes(src, src.readerIndex(), length);
    //     src.readerIndex(src.readerIndex() + length);
    //     return this;
    // }
    // PooledByteBuf* PooledByteBuf::writeBytes(PooledByteBuf* src, int srcIndex, int length){
    //     ensureWritable(length);
    //     setBytes(m_writerIndex, src, srcIndex, length);
    //     m_writerIndex += length;
    //     return this;
    // }

    // PooledDirectByteBuf.java
    PooledByteBuf* PooledByteBuf::setBytes(int index, const char* src, int srcIndex, int length){
        // 检查目标数组的可读空间是否足够
        // checkSrcIndex(index, length, srcIndex, src.length);
        // checkIndex(index, length);
        // 再次检测ByteBuf可写容量是否足够
        assert(index + length <= m_length);
        // /* 复制了一个新的直接内存缓冲区，往新复制的数组里边设置 */
        // version1
        // _internalNioBuffer(index, length, false).put(src, srcIndex, length);
        // index = idx(index);
        // ByteBuffer buffer = internalNioBuffer();
        // buffer.limit(index + length).position(index);
        // return buffer //.put(src, srcIndex, length);
        // version2
        // ByteBuffer tmpBuf = internalNioBuffer();
        // tmpBuf.clear().position(index).limit(index + length);
        // tmpBuf.put(src, srcIndex, length);
        // 将原数组的数据写入到ByteBuf中
        index = idx(index);
        // m_tmpBuf = m_memory + index;
        // m_tmpBuf.put(src, srcIndex, length);
        char * toWriteBuf = m_memory + index;
        std::copy(src + srcIndex, src + srcIndex + length, toWriteBuf);
        return this;
    }

    // AbstractByteBuf
    PooledByteBuf* PooledByteBuf::ensureWritable(int minWritableBytes){
        // 如果可写容量大于等于minWritableBytes，则说明可写，直接返回
        // 如果小于首先判断是否超过了最大可以容量
        // 如果超过则抛异常；如果没有则扩容
        assert(minWritableBytes >= 0);
        int targetCapacity = m_writerIndex + minWritableBytes;
        // using non-short-circuit & to reduce branching - this is a hot path and targetCapacity should rarely overflow
        // if (targetCapacity >= 0 & targetCapacity <= capacity()) {
        //     return;
        // }
        assert(targetCapacity >= 0 && targetCapacity <= maxCapacity());

        if(targetCapacity > capacity()){
            // Normalize the target capacity to the power of 2.
            int writable = writableBytes();
            int newCapacity = writable >= minWritableBytes ? m_writerIndex + writable
                    : m_allocator->calculateNewCapacity(targetCapacity, m_maxCapacity);

            // Adjust to the new capacity.
            // 在调用 calculateNewCapacity 计算完目标容量之后
            // 需要重新创建新的缓冲区，并将原缓冲区的内容复制到新创建的ByteBuf中
            // 这些都是调用capacity(newCapacity)中来完成的。
            // 由于不同的子类会对应不同的复制操作
            // 所以AbstractByteBuf类中的该方法是一个抽象方法，留给子类自己来实现
            capacity(newCapacity);
        }
        return this;
    }
    // AbstractByteBuf
    // void PooledByteBuf::ensureWritable0(int minWritableBytes){}

    //确保newCapacity是有效的，且不大于首次分配的maxCapacity
    void PooledByteBuf::checkNewCapacity(int newCapacity){
        // ensureAccessible();
        if (newCapacity < 0 || newCapacity > maxCapacity()) {
            throw std::exception();
        }
    }
    
    // PooledByteBuf
    PooledByteBuf* PooledByteBuf::capacity(int newCapacity){
        if (newCapacity == m_length) {
            return this;
        }
        checkNewCapacity(newCapacity);
        // if (!m_chunk->m_unpooled) {
            // If the request capacity does not require reallocation, just update the length of the memory.
        if (newCapacity > m_length) {
            if (newCapacity <= m_maxLength) {
                m_length = newCapacity;
                return this;
            }
        } else if (newCapacity > (m_maxLength >> 1) &&
                (m_maxLength > 512 || newCapacity > m_maxLength - 16)) {
            // here newCapacity < length
            m_length = newCapacity;
            trimIndicesToCapacity(newCapacity);
            return this;
        }
        //}

        // Reallocation required.
        m_chunk->m_arena->reallocate(this, newCapacity, true);
        return this;
    }

    // AbstractByteBuf
    void PooledByteBuf::trimIndicesToCapacity(int newCapacity){
        if (m_writerIndex > newCapacity) {
            setIndex0(std::min(m_readerIndex, newCapacity), newCapacity);
        }
    }

    // AbstractByteBuf
    PooledByteBuf* PooledByteBuf::clear(){
        m_readerIndex = m_writerIndex = 0;
        return this;
    }
}

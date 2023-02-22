#include "PoolArena.h"

namespace buffer{
    thread_local int probe = 0;

    PoolArena::PoolArena(PooledByteBufAllocator* parent, int pageSize,
            int pageShifts, int chunkSize)
    :SizeClasses(pageSize, pageShifts, chunkSize),
    m_deallocationsSmall(0),
    m_deallocationsNormal(0),
    m_numThreadCaches(0),
    m_allocationsNormal(0),
    m_parent(parent),
    m_numSmallSubpagePools(m_nSubpages){
        m_smallSubpagePools.assign(m_numSmallSubpagePools, nullptr);
        for (int i = 0; i < m_smallSubpagePools.size(); i ++) {
            m_smallSubpagePools[i] = newSubpagePoolHead();
        }
        // 使用INT_MAX INT_MIN会int越界
        q100 = new PoolChunkList(this, nullptr, 100, 8192, chunkSize);
        q075 = new PoolChunkList(this, q100, 75, 100, chunkSize);
        q050 = new PoolChunkList(this, q075, 50, 100, chunkSize);
        q025 = new PoolChunkList(this, q050, 25, 75, chunkSize);
        // q000没有前置节点，当一个chunk进入到q000列表
        // 如果其内存被完全释放，则不再保留在内存中，其分配的内存被完全回收。
        q000 = new PoolChunkList(this, q025, 1, 50, chunkSize);
        // qInit前置节点为自己，minUsage=INT_MIN
        // 意味着一个初分配的chunk，在最开始的内存分配过程中(内存使用率<25%)
        // 即使完全释放也不会被回收，会始终保留在内存中。
        qInit = new PoolChunkList(this, q000, -8192, 25, chunkSize);

        q100->prevList(q075);
        q075->prevList(q050);
        q050->prevList(q025);
        q025->prevList(q000);
        q000->prevList(nullptr);
        qInit->prevList(qInit);
    }

    PoolSubpage* PoolArena::newSubpagePoolHead(){
        PoolSubpage* head = new PoolSubpage();
        head->prev = head;
        head->next = head;
        return head;
    }

    PooledByteBuf* PoolArena::allocate(PoolThreadCache* cache, int reqCapacity, int maxCapacity){
        // newByteBuf()方法将会创建一个未经初始化的PooledByteBuf对象
        // 也就是说其内部的ByteBuffer和readerIndex，writerIndex等参数都是默认值
        PooledByteBuf* buf = newByteBuf(maxCapacity);
        // 使用allocate为创建的ByteBuf初始化相关内存数据
        allocate(cache, buf, reqCapacity);
        return buf;
    }
    // 首先会判断目标内存是在哪个内存层级的，small或者normal
    // 然后根据目标层级的分配方式对目标内存进行扩容(内存规整)
    // 1/首先会尝试从当前线程的缓存中申请目标内存，如果能够申请到，则直接返回
    // 2/如果不能申请到，则在当前层级中申请。对于tiny和small层级的内存申请
    // 3/如果无法申请到，则会将申请动作交由PoolChunkList进行(allocateNormal)
    void PoolArena::allocate(PoolThreadCache* cache, PooledByteBuf* buf, int reqCapacity){
        int sizeIdx = size2SizeIdx(reqCapacity);
        // 1、默认先尝试从poolThreadCache中分配内存，PoolThreadCache利用ThreadLocal的特性，消除了多线程竞争，提高内存分配效率
        // 首次分配时，poolThreadCache中并没有可用内存进行分配
        // 当上一次分配的内存使用完并释放时，会将其加入到poolThreadCache中，提供该线程下次申请时使用。
        // 2、如果是分配小内存，则尝试从smallSubpagePools中分配内存
        if (sizeIdx <= m_smallMaxSizeIdx) {
            tcacheAllocateSmall(cache, buf, reqCapacity, sizeIdx);
        } else if (sizeIdx < m_nSizes) {
            // 3、如果分配一个page以上的内存，直接采用方法allocateNormal分配内存。
            tcacheAllocateNormal(cache, buf, reqCapacity, sizeIdx);
        } else {
            int normCapacity = normalizeSize(reqCapacity);
            // Huge allocations are never served via the cache so just call allocateHuge
            allocateHuge(buf, normCapacity);
        }
    }

    void PoolArena::tcacheAllocateSmall(PoolThreadCache* cache, PooledByteBuf* buf, int reqCapacity, int sizeIdx){
        if (cache->allocateSmall(this, buf, reqCapacity, sizeIdx)) {
            // 从当前线程的缓存中尝试申请内存
            // 该方法中会使用申请到的内存对ByteBuf对象进行初始化
            // was able to allocate out of the cache so move on
            return;
        }
        // 如果没有合适subpage，则采用方法allocateNormal分配内存。
        PoolSubpage* head = m_smallSubpagePools[sizeIdx];
        bool needsNormalAllocation = false;
        {
            // 这里需要注意的是，由于对head进行了加锁，而在同步代码块中判断了s != head，
            // 也就是说PoolSubpage链表中是存在未使用的PoolSubpage的，因为如果该节点已经用完了，
            // 其是会被移除当前链表的。也就是说只要s != head，那么这里的allocate()方法
            // 就一定能够申请到所需要的内存块
            std::lock_guard<std::mutex> lock(head->m_mtx);
            PoolSubpage* s = head->next;
            // s != head就证明当前PoolSubpage链表中存在可用的PoolSubpage
            // 并且一定能够申请到内存
            // 因为已经耗尽的PoolSubpage是会从链表中移除的
            needsNormalAllocation = s == head;
            if (!needsNormalAllocation) {
                assert(s->m_doNotDestroy && s->m_elemSize == sizeIdx2size(sizeIdx));
                // 从PoolSubpage中申请内存
                long handle = s->allocate();
                assert(handle >= 0);
                // 通过申请的内存对ByteBuf进行初始化
                s->m_chunk->initBufWithSubpage(buf, nullptr, handle, reqCapacity, cache);
            
            }
        }
        // 没有可用的PoolSubpage，需要申请一个Normal级别的内存块，再在上面分配所需内存
        if (needsNormalAllocation) {
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                allocateNormal(buf, reqCapacity, sizeIdx, cache);
            }
        }

        m_allocationsSmall.add(1L);
    }

    void PoolArena::tcacheAllocateNormal(PoolThreadCache* cache, PooledByteBuf* buf, int reqCapacity, int sizeIdx){
        if (cache->allocateNormal(this, buf, reqCapacity, sizeIdx)) {
            // was able to allocate out of the cache so move on
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            allocateNormal(buf, reqCapacity, sizeIdx, cache);
            ++m_allocationsNormal;
        }
    }

    void PoolArena::allocateNormal(PooledByteBuf* buf, int reqCapacity, int sizeIdx, PoolThreadCache* threadCache){
        // 在PoolChunkList中申请内存时，本质上还是将申请动作交由其内部的PoolChunk进行申请
        // 如果申请到了，其还会判断当前PoolChunk的内存使用率是否超过了当前PoolChunkList的阈值
        // 如果超过了，则会将其移动到下一PoolChunkList中。
        // qInit ---> q000 <---> q025 <---> q050 <---> q075 <---> q100
        // 不优先从q000分配，正是因为q000上的PoolChunk内存完全释放后要被销毁，如果在上面分配，则会延迟内存的回收进度。
        // 而q075上由于内存利用率太高，导致内存分配的成功率大大降低，因此放到最后。
        // 在q050先分配，这样大部分情况下，Chunk的利用率都会保持在一个较高水平，提高整个应用的内存利用率；
        if (q050->allocate(buf, reqCapacity, sizeIdx, threadCache) ||
            q025->allocate(buf, reqCapacity, sizeIdx, threadCache) ||
            q000->allocate(buf, reqCapacity, sizeIdx, threadCache) ||
            qInit->allocate(buf, reqCapacity, sizeIdx, threadCache) ||
            q075->allocate(buf, reqCapacity, sizeIdx, threadCache)) {
            return;
        }

        // Add a new chunk.
        // 第一次进行内存分配时，chunkList没有chunk可以分配内存
        // 需通过方法newChunk新建一个chunk进行内存分配，并添加到qInit列表中。
        PoolChunk* c = newChunk(m_pageSize, m_nPSizes, m_pageShifts, m_chunkSize);
        bool success = c->allocate(buf, reqCapacity, sizeIdx, threadCache);
        assert(success);
        qInit->add(c);
    }

    void PoolArena::allocateHuge(PooledByteBuf* buf, int reqCapacity){
        PoolChunk* chunk = newUnpooledChunk(reqCapacity);
        m_activeBytesHuge.add(chunk->chunkSize());
        buf->initUnpooled(chunk, reqCapacity);
        m_allocationsHuge.add(1L);
    }

    void PoolArena::free(PoolChunk* chunk, char* buffer, long handle, int normCapacity, PoolThreadCache* cache){
        if (chunk->m_unpooled) {
            // 如果是非池化的，则直接销毁目标内存块，并且更新相关的数据
            int size = chunk->chunkSize();
            destroyChunk(chunk);
            m_activeBytesHuge.add(-size);
            m_deallocationsHuge.add(1L);
        } else {
            // 如果是池化的，首先判断其是哪种类型的，即tiny，small或者normal，
            // 然后将其交由当前线程的缓存进行处理，如果添加成功，则直接返回
            SizeClassType sizeClass = getSizeClassType(handle);
            if (cache != nullptr && cache->add(this, chunk, buffer, handle, normCapacity, sizeClass)) {
                // cached so not free it.
                return;
            }
            // 如果当前线程的缓存已满，则将目标内存块返还给公共内存块进行处理
            freeChunk(chunk, handle, normCapacity, sizeClass, buffer, false);
        }
    }

    enum SizeClassType PoolArena::getSizeClassType(long handle){
        return PoolChunk::isSubpage(handle) ? SMALL : NORMAL;  
    }

    void PoolArena::freeChunk(PoolChunk* chunk, long handle, int normCapacity, SizeClassType sizeClass, char* buffer, bool finalizer){
        bool destroyChunkSign;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            // We only call this if freeChunk is not called because of the PoolThreadCache finalizer as otherwise this
            // may fail due lazy class-loading in for example tomcat.
            if (!finalizer) {
                switch (sizeClass) {
                    case NORMAL:
                        ++m_deallocationsNormal;
                        break;
                    case SMALL:
                        ++m_deallocationsSmall;
                        break;
                    default:
                        throw std::exception();
                }
            }
            destroyChunkSign = !chunk->m_parent->free(chunk, handle, normCapacity, buffer);
        }
        if (destroyChunkSign) {
            // destroyChunk not need to be called while holding the synchronized lock.
            destroyChunk(chunk);
        }
    }
    PoolSubpage* PoolArena::findSubpagePoolHead(int sizeIdx){
        return m_smallSubpagePools[sizeIdx];
    }
    void PoolArena::reallocate(PooledByteBuf* buf, int newCapacity, bool freeOldMemory){
        assert(newCapacity >= 0 && newCapacity <= buf->maxCapacity());

        int oldCapacity = buf->length();
        if (oldCapacity == newCapacity) {
            return;
        }

        PoolChunk* oldChunk = buf->chunk();
        char* buffer = buf->tmpBuf();
        long oldHandle = buf->handle();
        char* oldMemory = buf->memory();
        int oldOffset = buf->offset();
        int oldMaxLength = buf->maxLength();

        // This does not touch buf's reader/writer indices
        allocate(m_parent->threadCache(), buf, newCapacity);
        int bytesToCopy;
        if (newCapacity > oldCapacity) {
            bytesToCopy = oldCapacity;
        } else {
            buf->trimIndicesToCapacity(newCapacity);
            bytesToCopy = newCapacity;
        }
        memoryCopy(oldMemory, oldOffset, buf, bytesToCopy);
        if (freeOldMemory) {
            free(oldChunk, buffer, oldHandle, oldMaxLength, buf->cache());
        }
    }

    DefaultArena::DefaultArena(PooledByteBufAllocator* parent, int pageSize, int pageShifts, int chunkSize)
    :PoolArena(parent, pageSize, pageShifts, chunkSize)
    {}

    // bool HeapArena::isDirect(){return false;}
    PoolChunk* DefaultArena::newChunk(int pageSize, int maxPageIdx, int pageShifts, int chunkSize) {
        return new PoolChunk(this, defaultAllocate(chunkSize), pageSize, pageShifts, chunkSize, maxPageIdx);
    }

    PoolChunk* DefaultArena::newUnpooledChunk(int capacity){
        return new PoolChunk(this, defaultAllocate(m_chunkSize), capacity);
    }

    // 如果分配如512字节的小内存，除了创建chunk，还有创建subpage，PoolSubpage在初始化之后，会添加到smallSubpagePools中，其实并不是直接插入到数组，而是添加到head的next节点。下次再有分配512字节的需求时，直接从smallSubpagePools获取对应的subpage进行分配。
    // static byte[] newByteArray(int size)
    char* DefaultArena::defaultAllocate(int size){
        return new char[size];
    }   

    void DefaultArena::destroyChunk(PoolChunk* chunk){ 
        delete chunk;
    }

    PooledByteBuf* DefaultArena::newByteBuf(int maxCapacity){
        return PooledByteBuf::newInstance(maxCapacity);
    }

    void DefaultArena::memoryCopy(const char* memory, int srcOffset, PooledByteBuf* dstbuffer, int length){
        if(length == 0) return ;
        std::copy(memory + srcOffset, memory + srcOffset + length, dstbuffer->memory() + dstbuffer->offset());
    }
}

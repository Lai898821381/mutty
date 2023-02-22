#ifndef BUFFER_POOLBYTEBUFALLOCATOR_H
#define BUFFER_POOLBYTEBUFALLOCATOR_H

#include <limits.h>
#include <unistd.h>
#include <mutex>
#include <vector>
#include "MathUtil.h"
#include "PoolThreadCache.h"
#include "PooledByteBuf.h"
/*
PooledByteBufAllocator中主要维护了三个属性

PoolArena<byte[]>[] heapArenas
PoolArena[] directArenas
PoolThreadLocalCache threadCache;
其中heapArenas和directArenas分别维护堆内存和堆外内存块，而threadCache用于维护线程本地缓存对象，这些都是netty内存管理体系的重要组成部分。
*/
namespace buffer{
    class MathUtil;
    class PoolArena;
    class PoolThreadCache;
    class PooledByteBuf;
    class PooledByteBufAllocator
    {
        void PooledByteBufAllocatorInit(int nArena, int pageSize, int maxOrder, int smallCacheSize, int normalCacheSize);
        static std::vector<PoolArena*> newArenaArray(int size);
        static int validateAndCalculatePageShifts(int pageSize);
        static int validateAndCalculateChunkSize(int pageSize, int maxOrder);
        static void validate(int initialCapacity, int maxCapacity);
        PooledByteBuf* newPoolBuffer(int initialCapacity, int maxCapacity);        
        static PoolArena* leastUsedArena(std::vector<PoolArena *>& arenas);
        PooledByteBufAllocator();

        static thread_local PoolThreadCache * m_pooledTheadCache;
        static std::vector<PoolArena *> m_arenas;
        static int m_smallCacheSize;
        static int m_normalCacheSize;
        static int m_chunkSize;

        static PooledByteBufAllocator* DEFAULT_ALLOC;
        static std::mutex m_lockSingleton;
        static std::mutex m_lockThreadcache;
    public:
        static int DEFAULT_NUM_ARENA;
        // 必须>4KB chunksize=pagesize*(2^max_order)
        static const int DEFAULT_PAGE_SIZE = 8192;
        // 0-14
        static const int DEFAULT_MAX_ORDER = 11; // 8192 << 11 = 16 MiB per chunk
        static const int DEFAULT_SMALL_CACHE_SIZE = 256;
        static const int DEFAULT_NORMAL_CACHE_SIZE = 64;
        static const int DEFAULT_MAX_CACHED_BUFFER_CAPACITY = 32 * 1024;
        static const int DEFAULT_CACHE_TRIM_INTERVAL = 8192;
        // static const long DEFAULT_CACHE_TRIM_INTERVAL_MILLIS = 0L;
        static const int DEFAULT_MAX_CACHED_BYTEBUFFERS_PER_CHUNK = 1023;
        static const int MIN_PAGE_SIZE = 4096;
        static const int MAX_CHUNK_SIZE = (int) (((long) INT_MAX + 1) / 2);
        static const int DEFAULT_INITIAL_CAPACITY = 256;
        static const int DEFAULT_MAX_CAPACITY = INT_MAX;
        static const int DEFAULT_MAX_COMPONENTS = 16;
        static const int CALCULATE_THRESHOLD = 1048576 * 4; // 4 MiB page

        PooledByteBufAllocator(const PooledByteBufAllocator&) = delete;

        PooledByteBuf* buffer();
        PooledByteBuf* buffer(int initialCapacity);
        PooledByteBuf* buffer(int initialCapacity, int maxCapacity);
        static PooledByteBufAllocator* ALLOCATOR();

        static PoolThreadCache* initialValue();
        PoolThreadCache* threadCache();
        int calculateNewCapacity(int minNewCapacity, int maxCapacity);

    };
}

#endif //BUFFER_POOLBYTEBUFALLOCATOR_H
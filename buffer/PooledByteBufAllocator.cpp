#include "PooledByteBufAllocator.h"

namespace buffer{
    PooledByteBufAllocator::PooledByteBufAllocator(){
        assert(DEFAULT_NUM_ARENA > 0);
        PooledByteBufAllocatorInit(DEFAULT_NUM_ARENA, DEFAULT_PAGE_SIZE, DEFAULT_MAX_ORDER,
                                  DEFAULT_SMALL_CACHE_SIZE, DEFAULT_NORMAL_CACHE_SIZE);
    }
    PooledByteBufAllocator* PooledByteBufAllocator::DEFAULT_ALLOC = nullptr;

    int PooledByteBufAllocator::m_smallCacheSize = 0;
    int PooledByteBufAllocator::m_normalCacheSize = 0;
    int PooledByteBufAllocator::m_chunkSize = 0;
    std::vector<PoolArena *> PooledByteBufAllocator::m_arenas;
    std::mutex PooledByteBufAllocator::m_lockSingleton;
    std::mutex PooledByteBufAllocator::m_lockThreadcache;

    thread_local PoolThreadCache* PooledByteBufAllocator::m_pooledTheadCache(nullptr);
    int  PooledByteBufAllocator::DEFAULT_NUM_ARENA = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));

    PooledByteBufAllocator* PooledByteBufAllocator::ALLOCATOR(){
        // LazySingleton
        if(DEFAULT_ALLOC == nullptr){
            std::lock_guard<std::mutex> lock(m_lockSingleton);
            if(DEFAULT_ALLOC == nullptr){
                // 双重加锁检查
                DEFAULT_ALLOC = new PooledByteBufAllocator();
            }
        }
        if(m_pooledTheadCache == nullptr){
            m_pooledTheadCache = initialValue();
        }
        return DEFAULT_ALLOC;
    }

    PoolThreadCache* PooledByteBufAllocator::initialValue(){
        PoolArena* leastArena;
        {
            std::lock_guard<std::mutex> lock(m_lockThreadcache);
            leastArena = leastUsedArena(m_arenas);
        }
        return new PoolThreadCache(leastArena, m_smallCacheSize, m_normalCacheSize,
                        DEFAULT_MAX_CACHED_BUFFER_CAPACITY, DEFAULT_CACHE_TRIM_INTERVAL);
    }

    PoolArena* PooledByteBufAllocator::leastUsedArena(std::vector<PoolArena *>& arenas){
        if (arenas.empty()) return nullptr;
        PoolArena* minArena = arenas[0];
        for (int i = 1; i < arenas.size(); i++) {
            PoolArena* arena = arenas[i];
            if (arena->m_numThreadCaches.load() < minArena->m_numThreadCaches.load()) {
                minArena = arena;
            }
        }

        return minArena;
    }

    void PooledByteBufAllocator::PooledByteBufAllocatorInit(int nArena, int pageSize, int maxOrder,
                                int smallCacheSize, int normalCacheSize){
        // threadCache = new PoolThreadLocalCache(false);
        // directMemoryCacheAlignment = 0;
        m_smallCacheSize = smallCacheSize;
        m_normalCacheSize = normalCacheSize;
        m_chunkSize = validateAndCalculateChunkSize(pageSize, maxOrder);
        int pageShifts = validateAndCalculatePageShifts(pageSize);

        if (nArena > 0) {
            m_arenas = newArenaArray(nArena);
            // List<PoolArenaMetric> metrics = new ArrayList<PoolArenaMetric>(directArenas.length);
            for (int i = 0; i < m_arenas.size(); i ++) {
                PoolArena* arena = new DefaultArena(this, pageSize, pageShifts, m_chunkSize);
                m_arenas[i] = arena;
                // metrics.add(arena);
            }
            // directArenaMetrics = Collections.unmodifiableList(metrics);
        }
    }

    std::vector<PoolArena*> PooledByteBufAllocator::newArenaArray(int size){
        std::vector<PoolArena*> pooledArenaArray(size, nullptr);
        return pooledArenaArray;
    }

    int PooledByteBufAllocator::validateAndCalculatePageShifts(int pageSize){
        if (pageSize < MIN_PAGE_SIZE) throw std::exception();
        if ((pageSize & pageSize - 1) != 0) throw std::exception();
        return MathUtil::log2(pageSize);
    }
    
    int PooledByteBufAllocator::validateAndCalculateChunkSize(int pageSize, int maxOrder){
        assert(maxOrder < 14);

        // Ensure the resulting chunkSize does not overflow.
        int chunkSize = pageSize;
        for (int i = maxOrder; i > 0; i --) {
            if (chunkSize > MAX_CHUNK_SIZE / 2)
                throw std::exception();
            chunkSize <<= 1;
        }
        return chunkSize;
    }

    PooledByteBuf* PooledByteBufAllocator::buffer(){
        return newPoolBuffer(DEFAULT_INITIAL_CAPACITY, DEFAULT_MAX_CAPACITY);
    }

    PooledByteBuf* PooledByteBufAllocator::buffer(int initialCapacity){
        return newPoolBuffer(initialCapacity, DEFAULT_MAX_CAPACITY);
    }

    // AbstractByteBufAllocator
    PooledByteBuf* PooledByteBufAllocator::buffer(int initialCapacity, int maxCapacity){
        if (initialCapacity == 0 && maxCapacity == 0) {
            return nullptr;
        }
        validate(initialCapacity, maxCapacity);
        return newPoolBuffer(initialCapacity, maxCapacity);
    }

    void PooledByteBufAllocator::validate(int initialCapacity, int maxCapacity){
        assert(initialCapacity >= 0);
        if (initialCapacity > maxCapacity) {
            throw std::exception();
        }
    }

    PooledByteBuf* PooledByteBufAllocator::newPoolBuffer(int initialCapacity, int maxCapacity){
        // 从当前线程缓存中获取对应内存池PoolArena
        PoolArena* defaultArena = m_pooledTheadCache->m_arena;
        PooledByteBuf* buf = nullptr;
        if (defaultArena != nullptr) {
            // 在当前线程内存池上分配内存
            buf = defaultArena->allocate(m_pooledTheadCache, initialCapacity, maxCapacity);
        } else throw std::exception();
        return buf;  
    }
    PoolThreadCache* PooledByteBufAllocator::threadCache(){
        assert(m_pooledTheadCache != nullptr);
        return m_pooledTheadCache;
    }

    int PooledByteBufAllocator::calculateNewCapacity(int minNewCapacity, int maxCapacity){
        assert(minNewCapacity >= 0);
        if (minNewCapacity > maxCapacity) {
            throw std::exception();
        }
        int threshold = CALCULATE_THRESHOLD; // 4 MiB page

        if (minNewCapacity == threshold) {
            return threshold;
        }

        // If over threshold, do not double but just increase by threshold.
        if (minNewCapacity > threshold) {
            int newCapacity = minNewCapacity / threshold * threshold;
            if (newCapacity > maxCapacity - threshold) {
                newCapacity = maxCapacity;
            } else {
                newCapacity += threshold;
            }
            return newCapacity;
        }
        // 64 <= newCapacity is a power of 2 <= threshold
        int newCapacity = 64;
        while(newCapacity < minNewCapacity) {
            newCapacity <<= 1;
        }
        return std::min(newCapacity, maxCapacity);
    }

    
}
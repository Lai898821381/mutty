#include "PoolThreadCache.h"

namespace buffer{
    PoolThreadCache::PoolThreadCache(PoolArena* arena, int smallCacheSize, int normalCacheSize,
            int maxCachedBufferCapacity, int freeSweepAllocationThreshold)
    :m_freeSweepAllocationThreshold(freeSweepAllocationThreshold),
    m_arena(arena),
    m_allocations(0){
        assert(maxCachedBufferCapacity >= 0);
        if (m_arena != nullptr) {
            m_smallSubPageCaches = createSubPageCaches(smallCacheSize, arena->m_numSmallSubpagePools);
            m_normalCaches = createNormalCaches(normalCacheSize, maxCachedBufferCapacity, arena);
            (arena->m_numThreadCaches)++;
        } else {
            m_smallSubPageCaches.clear();
            m_normalCaches.clear();
        }

        // Only check if there are caches in use.
        if ((!m_smallSubPageCaches.empty() || !m_normalCaches.empty()) && m_freeSweepAllocationThreshold < 1)
            throw std::exception();
    }

    // 直接调用free(true)?
    PoolThreadCache::~PoolThreadCache() {
        for(MemoryRegionCache* ptr : m_smallSubPageCaches){
            if(ptr != nullptr) delete ptr;
        }
        for(MemoryRegionCache* ptr : m_normalCaches){
            if(ptr != nullptr) delete ptr;
        }     
    }

    std::vector<MemoryRegionCache*> PoolThreadCache::createSubPageCaches(
            int cacheSize, int numCaches){
        std::vector<MemoryRegionCache*> cache;
        if (cacheSize > 0 && numCaches > 0) {
            for (int i = 0; i < numCaches; i++) {
                cache.push_back(new SubPageMemoryRegionCache(cacheSize));
            }
        }
        return cache;
    }

    std::vector<MemoryRegionCache*> PoolThreadCache::createNormalCaches(
            int cacheSize, int maxCachedBufferCapacity, PoolArena* area){
        std::vector<MemoryRegionCache*> cache;
        if (cacheSize > 0 && maxCachedBufferCapacity > 0) {
            int maxSize = std::min(area->m_chunkSize, maxCachedBufferCapacity);
            // Create as many normal caches as we support based on how many sizeIdx we have and what the upper
            // bound is that we want to cache in general.
            for (int idx = area->m_numSmallSubpagePools; idx < area->m_nSizes && area->sizeIdx2size(idx) <= maxSize ; idx++) {
                cache.push_back(new NormalMemoryRegionCache(cacheSize));
            }
        }
        return cache;
    }

    bool PoolThreadCache::allocate(MemoryRegionCache* cache, PooledByteBuf* buf, int reqCapacity){
        if(cache == nullptr) return false;
        // 从MemoryRegionCache中申请内存
        // 本质上就是从其队列中申请，如果存在，则初始化申请到的内存块
        bool allocated = cache->allocate(buf, reqCapacity, this);
        // 这里是如果当前PoolThreadCache中申请内存的次数达到了8192次
        // 则对内存块进行一次trim()操作，
        // 对使用较少的内存块，将其返还给PoolArena，以供给其他线程使用
        if (++ m_allocations >= m_freeSweepAllocationThreshold) {
            m_allocations = 0;
            trim();
        }
        return allocated;
    }

    MemoryRegionCache* PoolThreadCache::genCache(PoolArena* area, int sizeIdx, SizeClassType sizeClass){
        switch (sizeClass) {
        case NORMAL:
            return cacheForNormal(area, sizeIdx);
        case SMALL:
            return cacheForSmall(area, sizeIdx);
        default:
            throw std::exception();
        }
    }

    int PoolThreadCache::free(std::vector<MemoryRegionCache*>& caches, bool finalizer){
        if (caches.empty()) return 0;
        int numFreed = 0;
        for (MemoryRegionCache* c : caches) {
            numFreed += free(c, finalizer);
        }
        return numFreed;
    }

    int PoolThreadCache::free(MemoryRegionCache* cache, bool finalizer){
        if (cache == nullptr) return 0;
        return cache->free(finalizer);
    }

    void PoolThreadCache::trim(std::vector<MemoryRegionCache*>& caches){
        if (caches.empty()) return ;
        for (MemoryRegionCache* c: caches) {
            trim(c);
        }
    }

    void PoolThreadCache::trim(MemoryRegionCache* cache){
        if (cache == nullptr) return ;
        cache->trim();
    }

    MemoryRegionCache* PoolThreadCache::cacheForSmall(PoolArena* area, int sizeIdx){
        return cache(m_smallSubPageCaches, sizeIdx);
    }

    MemoryRegionCache* PoolThreadCache::cacheForNormal(PoolArena* area, int sizeIdx){
        int idx = sizeIdx - area->m_numSmallSubpagePools;
        // // 返回small类型的数组中对应位置的MemoryRegionCache
        return cache(m_normalCaches, idx);
    }

    bool PoolThreadCache::allocateSmall(PoolArena* area, PooledByteBuf* buf, int reqCapacity, int sizeIdx){
        return allocate(cacheForSmall(area, sizeIdx), buf, reqCapacity);
    }

    bool PoolThreadCache::allocateNormal(PoolArena* area, PooledByteBuf* buf, int reqCapacity, int sizeIdx){
        return allocate(cacheForNormal(area, sizeIdx), buf, reqCapacity);
    }

    bool PoolThreadCache::add(PoolArena* area, PoolChunk* chunk, char* buffer, long handle,
                              int normCapacity, SizeClassType sizeClass){
        int sizeIdx = area->size2SizeIdx(normCapacity);
        // 通过当前释放的内存块的大小计算其应该放到哪个等级的MemoryRegionCache中
        MemoryRegionCache* cache = genCache(area, sizeIdx, sizeClass);
        if (cache == nullptr) {
            return false;
        }
        // 将内存块释放到目标MemoryRegionCache中
        return cache->add(chunk, buffer, handle, normCapacity);
    }

    // finalize:free(true)    onRemoval:free(false)
    // 但是onRemoval未被调用？所以总是true
    void PoolThreadCache::free(bool finalizer){
        bool com = false;
        // 保证只被调用一次
        if (m_toFree.compare_exchange_strong(com, true)) {
            int numFreed = free(m_smallSubPageCaches, finalizer) +
                    free(m_normalCaches, finalizer);

            if (m_arena != nullptr) {
                (m_arena->m_numThreadCaches)++;
            }
        }
        if(m_arena != nullptr){
            --m_arena->m_numThreadCaches;
        }
    }

    int MemoryRegionCache::free(bool finalizer) {
         return free(INT_MAX, finalizer);
    }

    void PoolThreadCache::trim(){
        // 内存总的申请次数达到8192时遍历其所有的MemoryRegionCache
        // 调用trim()方法进行内存释放
        trim(m_smallSubPageCaches);
        trim(m_normalCaches);
    }

    MemoryRegionCache::Entry::~Entry(){
        if(m_chunk != nullptr) delete m_chunk;
        if(buffer != nullptr) delete[] buffer;
    }

    int MemoryRegionCache::free(int max, bool finalizer){
        int numFreed = 0;
        // 依次从队列中取出Entry数据，调用freeEntry()方法释放该Entry
        for (; numFreed < max; numFreed++) {
            // all cleared
            if(m_queue.empty()) return numFreed;
            else{
                Entry* entry = m_queue.front();
                m_queue.pop();
                freeEntry(entry, finalizer); 
            }
        }
        return numFreed;
    }

    void MemoryRegionCache::freeEntry(Entry* entry, bool finalizer){
        // 通过当前Entry中保存的PoolChunk和handle等数据释放当前内存块
        PoolChunk* chunk = entry->m_chunk;
        long handle = entry->handle;
        char* buffer = entry->buffer;

        if (!finalizer) {
            // recycle now so PoolChunk can be GC'ed. This will only be done if this is not freed because of
            // a finalizer.
            entry->recycle();
        }

        chunk->m_arena->freeChunk(chunk, handle, entry->normCapacity, m_sizeClassType, buffer, finalizer);
    }

    Recycler<MemoryRegionCache::Entry>* MemoryRegionCache::Entry::m_recycler = new MemoryRegionCache::EntryBufRecycler();

    MemoryRegionCache::Entry* MemoryRegionCache::newEntry(PoolChunk* chunk, const char* buffer, long handle, int normCapacity){
        Entry* entry = Entry::m_recycler->get();
        entry->m_chunk = chunk;
        entry->buffer = const_cast<char*> (buffer);
        entry->handle = handle;
        entry->normCapacity = normCapacity;
        return entry;
    }

    bool MemoryRegionCache::add(PoolChunk* chunk, const char* buffer, long handle, int normCapacity){
        // 这里会尝试从缓存中获取一个Entry对象，如果没获取到则创建一个
        Entry* entry = newEntry(chunk, buffer, handle, normCapacity);
        // 将实例化的Entry对象放到队列里
        m_queue.push(entry);
        return true;
    }

    bool MemoryRegionCache::allocate(PooledByteBuf* buf, int reqCapacity, PoolThreadCache* threadCache){
        // 尝试从队列中获取，如果队列中不存在，说明没有对应的内存块，则返回false，表示申请失败
        if(m_queue.empty()) return false;
        Entry* entry = m_queue.front();
        m_queue.pop();
        // 走到这里说明队列中存在对应的内存块，那么通过其存储的Entry对象来初始化ByteBuf对象，
        // 如此即表示申请内存成功
        initBuf(entry->m_chunk, entry->buffer, entry->handle, buf, reqCapacity, threadCache);
        // 对entry对象进行循环利用
        entry->recycle();

        // allocations is not thread-safe which is fine as this is only called from the same thread all time.
        // 对内存块使用次数进行计数，
        // 如果一个ThreadPoolCache所缓存的内存块使用较少
        // 那么就可以将其释放到PoolArena中，以便于其他线程可以申请使用。
        ++ m_allocations;
        return true;
    }

    void MemoryRegionCache::trim(){
        // size表示当前MemoryRegionCache中队列的最大可存储容量，allocations表示当前MemoryRegionCache
        // 的内存申请次数，size-allocations的含义就是判断当前申请的次数是否连队列的容量都没达到
        int toFreeNum = m_size - m_allocations;
        m_allocations = 0;
        // We not even allocated all the number that are
        // 申请的次数连队列的容量都没达到，则释放该内存块
        if (toFreeNum > 0) {
            free(toFreeNum, false);
        }
    }

    void SubPageMemoryRegionCache::initBuf(PoolChunk* chunk, const char* buffer, long handle, PooledByteBuf* buf, int reqCapacity,
              PoolThreadCache* threadCache) {
          chunk->initBufWithSubpage(buf, buffer, handle, reqCapacity, threadCache);
    }


    SubPageMemoryRegionCache::~SubPageMemoryRegionCache(){
            while(!m_queue.empty()) {
            if(m_queue.front() != nullptr) {
                delete m_queue.front();
                m_queue.pop();
            }
        }
    }

    void NormalMemoryRegionCache::initBuf(PoolChunk* chunk, const char* buffer, long handle, PooledByteBuf* buf, int reqCapacity,
            PoolThreadCache* threadCache) {
        chunk->initBuf(buf, buffer, handle, reqCapacity, threadCache);
    }

    NormalMemoryRegionCache::~NormalMemoryRegionCache(){
        while(!m_queue.empty()) {
            if(m_queue.front() != nullptr) {
                delete m_queue.front();
                m_queue.pop();
            }
        }
    }
}





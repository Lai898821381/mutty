#ifndef BUFFER_POOLTHREADCACHE_H
#define BUFFER_POOLTHREADCACHE_H

#include <atomic>
#include <vector>
#include <queue>
#include <numeric>
#include "commons.h"
#include "Recycler.h"
#include "MathUtil.h"
#include "PoolArena.h"
#include "PoolChunk.h"

/*                             smallSubpagePools
                              -------------------------          queue.size=512
PoolThreadCache -----|--------|MemoryRegionCache-16   |----------Entry Entry Entry
                     |        |MemoryRegionCache-32   |
                     |        |                       |
                     |        |                       |
                     |        -------------------------
                     |         normalSubpagePools
                     |         -------------------------         queue.size=64
                     |---------|MemoryRegionCache-2<<13|---------Entry Entry Entry
                     |         |MemoryRegionCache-2<<14|
                     |         |MemoryRegionCache-2<<15|
                     |         -------------------------
PoolThreadCache中有两个MemoryRegionCache数组用来存储small和normal内存块
每个MemoryRegionCache中有一个队列，队列中的元素为Entry类型
Entry用于存储缓存的内存块，通过记录当前内存块所在的PoolChunk和handle

PoolThreadCache为每一个线程关联一个PoolArena（PoolThreadCache#directArena）
该线程的内存都在该PoolArena上分配。
为了缓解线程竞争，通过创建多个PoolArena细化锁的粒度，从而提高并发执行的效率。
一个PoolArena可以会分给多个的线程，PoolArena上会有一些同步操作。
*/

namespace buffer{
  class PoolArena;
  class MemoryRegionCache;
  class PoolChunk;
  class PooledByteBuf;
  class PoolThreadCache
  {
    // PoolThreadCache中的内存块都是在当前线程使用完创建的ByteBuf对象后
    // 通过调用其release()方法释放内存时直接缓存到当前PoolThreadCache中的
    // 其并不会直接将内存块返回给PoolArena。
    static std::vector<MemoryRegionCache*> createSubPageCaches(int cacheSize, int numCaches);
    static std::vector<MemoryRegionCache*> createNormalCaches(int cacheSize, int maxCachedBufferCapacity, PoolArena* area);
    // 并不是新申请一块内存，而是其本身已经缓存有内存块
    // 如果当前申请的内存块大小正好与其一致，就会将该内存块返回；
    bool allocate(MemoryRegionCache* cache, PooledByteBuf* buf, int reqCapacity);
    MemoryRegionCache* genCache(PoolArena* area, int sizeIdx, SizeClassType sizeClass);
    static int free(std::vector<MemoryRegionCache*>& caches, bool finalizer);
    static int free(MemoryRegionCache* cache, bool finalizer);
    static void trim(std::vector<MemoryRegionCache*>& caches);
    static void trim(MemoryRegionCache* cache);
    MemoryRegionCache* cacheForSmall(PoolArena* area, int sizeIdx);
    MemoryRegionCache* cacheForNormal(PoolArena* area, int sizeIdx);

    std::vector<MemoryRegionCache*> m_smallSubPageCaches;
    std::vector<MemoryRegionCache*> m_normalCaches;

    std::atomic_bool m_toFree = ATOMIC_VAR_INIT(false);
    int m_allocations;
    int m_freeSweepAllocationThreshold;
    

    public:
      PoolThreadCache(PoolArena* arena, int smallCacheSize, int normalCacheSize,
             int maxCachedBufferCapacity, int freeSweepAllocationThreshold);
      ~PoolThreadCache();
      // 在对应的内存数组中找到MemoryRegionCache对象之后，通过调用allocate()方法来申请内存
      // 申请完之后还会检查当前缓存申请次数是否达到了8192次
      // 达到了则对缓存中使用的内存块进行检测，将较少使用的内存块返还给PoolArena。
      bool allocateSmall(PoolArena* area, PooledByteBuf* buf, int reqCapacity, int sizeIdx);
      bool allocateNormal(PoolArena* area, PooledByteBuf* buf, int reqCapacity, int sizeIdx);
      bool add(PoolArena* area, PoolChunk* chunk, char* buffer, long handle, int normCapacity, SizeClassType sizeClass);
      void free(bool finalizer);
      void trim();

      inline MemoryRegionCache* cache(std::vector<MemoryRegionCache*> cache, int sizeIdx){
        if (cache.empty() || sizeIdx > cache.size() - 1) {
            return nullptr;
        }
        return cache[sizeIdx];
      }

      PoolArena* m_arena;
  };

  class MemoryRegionCache{
    // PoolThreadCache维护每一个内存块最终都是使用的一个Entry对象来进行的
    struct Entry{
      static Recycler<Entry>* m_recycler;
      // 记录当前内存块是从哪一个PoolChunk中申请得来的
      PoolChunk* m_chunk;
      // 如果是直接内存，该属性记录了当前内存块所在的ByteBuffer对象
      char* buffer;
      // 将当前内存块释放到PoolArena中时，快速获取其所在的位置
      // 记录内存块在PoolChunk以及PoolSubpage中的位置
      long handle;
      int normCapacity;

      Entry()
      :m_chunk(nullptr),
      buffer(nullptr),
      handle(-1L),
      normCapacity(0)
      {}

      ~Entry();

      void recycle(){
        m_chunk = nullptr;
        buffer = nullptr;
        handle = -1;
        m_recycler->recycle(this);
      }

    };

    class EntryBufRecycler:public Recycler<Entry>{
        Entry* newObject(){
          return new Entry();
        }
    };

    int m_size;
    SizeClassType m_sizeClassType;
    int m_allocations;

    int free(int max, bool finalizer);
    void freeEntry(Entry* entry, bool finalizer);
    Entry* newEntry(PoolChunk* chunk, const char* buffer, long handle, int normCapacity);

    protected:
      virtual void initBuf(PoolChunk* chunk, const char* buffer, long handle,
                                        PooledByteBuf* buf, int reqCapacity, PoolThreadCache* threadCache) = 0;

      std::queue<Entry *> m_queue;

    public:
      MemoryRegionCache(int size, SizeClassType sizeClass)
          :m_size(MathUtil::nextPowerofTwo(size)),
          m_sizeClassType(sizeClass),
          m_allocations(0) {};
      virtual ~MemoryRegionCache() {};
      bool add(PoolChunk* chunk, const char* buffer, long handle, int normCapacity);
      bool allocate(PooledByteBuf* buf, int reqCapacity, PoolThreadCache* threadCache);
      int free(bool finalizer);
      void trim();
  };

  class SubPageMemoryRegionCache final:public MemoryRegionCache {
    public:
      explicit SubPageMemoryRegionCache(int size)
      :MemoryRegionCache(size, SMALL)
      {}

      void initBuf(PoolChunk* chunk, const char* buffer, long handle, PooledByteBuf* buf, int reqCapacity, PoolThreadCache* threadCache);

      ~SubPageMemoryRegionCache() override;
  };
  
  class NormalMemoryRegionCache final:public MemoryRegionCache {
      public:
        explicit NormalMemoryRegionCache(int size)
        :MemoryRegionCache(size, NORMAL)
        {}

        void initBuf(PoolChunk* chunk, const char* buffer, long handle, PooledByteBuf* buf, int reqCapacity, PoolThreadCache* threadCache);

        ~NormalMemoryRegionCache() override;
    };
}
#endif //BUFFER_POOLTHREADCACHE_H

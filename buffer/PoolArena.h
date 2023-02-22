#ifndef BUFFER_POOLARENA_H
#define BUFFER_POOLARENA_H

#include <vector>
#include <atomic>
#include <numeric>
#include <mutex>
#include "commons.h"
#include "PoolSubpage.h"
#include "SizeClasses.h"
#include "PooledByteBuf.h"
#include "PooledByteBufAllocator.h"
#include "LongAdder/LongAdder.h"

namespace buffer{
  class LongAdder;
  class PooledByteBuf;
  class PooledByteBufAllocator;
  class PoolSubpage;
  class PoolChunkList;
  // PoolArena负责管理PoolChunk和PoolSubpage
  // 创建多个PoolArena可以细化锁的粒度，提高并发效率
  class PoolArena: public SizeClasses{
      std::vector<PoolSubpage*> m_smallSubpagePools;
      PoolChunkList* q050;
      PoolChunkList* q025;
      PoolChunkList* q000;
      PoolChunkList* qInit;
      PoolChunkList* q075;
      PoolChunkList* q100;
      // ？以下变量实际并没有什么用处
      long m_allocationsNormal;
      // We need to use the LongCounter here as this is not guarded via synchronized block.
      LongAdder m_allocationsSmall;
      LongAdder m_allocationsHuge;
      LongAdder m_activeBytesHuge;

      long m_deallocationsSmall;
      long m_deallocationsNormal;
      LongAdder m_deallocationsHuge;

      std::mutex m_mtx;

      PoolSubpage* newSubpagePoolHead();
      void allocate(PoolThreadCache* cache, PooledByteBuf* buf, int reqCapacity);
      void tcacheAllocateSmall(PoolThreadCache* cache, PooledByteBuf* buf, int reqCapacity, int sizeIdx);
      void tcacheAllocateNormal(PoolThreadCache* cache, PooledByteBuf* buf, int reqCapacity, int sizeIdx);
      void allocateNormal(PooledByteBuf* buf, int reqCapacity, int sizeIdx, PoolThreadCache* threadCache);
      void allocateHuge(PooledByteBuf* buf, int reqCapacity);

    public:
      PoolArena(PooledByteBufAllocator* parent, int pageSize, int pageShifts, int chunkSize);
      PooledByteBuf* allocate(PoolThreadCache* cache, int reqCapacity, int maxCapacity);
      void free(PoolChunk* chunk, char* buffer, long handle, int normCapacity, PoolThreadCache* cache);
      SizeClassType getSizeClassType(long handle);
      void freeChunk(PoolChunk* chunk, long handle, int normCapacity, SizeClassType sizeClass, char* buffer, bool finalizer);
      PoolSubpage* findSubpagePoolHead(int sizeIdx);
      void reallocate(PooledByteBuf* buf, int newCapacity, bool freeOldMemory);

      virtual PoolChunk* newChunk(int pageSize, int maxPageIdx, int pageShifts, int chunkSize) = 0;
      virtual PoolChunk* newUnpooledChunk(int capacity) = 0;
      virtual PooledByteBuf* newByteBuf(int maxCapacity) = 0;
      virtual void memoryCopy(const char* src, int srcOffset, PooledByteBuf* dst, int length) = 0;
      virtual void destroyChunk(PoolChunk* chunk) = 0;
      std::vector<PoolSubpage*> newSubpagePoolArray(int size);

      int m_numSmallSubpagePools;
      PooledByteBufAllocator* m_parent;
      // 记录了当前PoolArena已经被多少个线程使用了
      // 在每一个线程申请新内存的时候，其会找到使用最少的那个
      // PoolArena进行内存的申请，这样可以减少线程之间的竞争
      std::atomic<int> m_numThreadCaches;
  };

  class DefaultArena:public PoolArena{
      PoolChunk* newChunk(int pageSize, int maxPageIdx, int pageShifts, int chunkSize);
      PoolChunk* newUnpooledChunk(int capacity);
      static char* defaultAllocate(int size);
      void destroyChunk(PoolChunk* chunk);
      PooledByteBuf* newByteBuf(int maxCapacity);
  
    public:
      DefaultArena(PooledByteBufAllocator* parent, int pageSize, int pageShifts, int chunkSize);
      void memoryCopy(const char* src, int srcOffset, PooledByteBuf* dst, int length);
      // void finalize();
      // void destroyPoolSubPages(PoolSubpage* pages);
      // void destroyPoolChunkLists(PoolChunkList... chunkLists);
  };
}

#endif // BUFFER_POOLARENA_H
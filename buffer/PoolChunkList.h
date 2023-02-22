#ifndef BUFFER_POOLCHUNKLIST_H
#define BUFFER_POOLCHUNKLIST_H

#include <numeric>
#include <memory>
#include "assert.h"
#include "PoolArena.h"
#include "PoolChunk.h"
#include "PooledByteBuf.h"

namespace buffer{
  class PoolArena;
  class PoolChunk;
  class PooledByteBuf;
  class PoolThreadCache;

  // 负责管理多个chunk的生命周期，在此基础上对内存分配进行进一步的优化。
  // 记录内存使用率在某个范围内的Chunk：是Chunk的内存使用率在某个范围
  class PoolChunkList{

      int calculateMaxCapacity(int minUsage, int chunkSize);
      bool move(PoolChunk* chunk);
      bool move0(PoolChunk* chunk);
      void remove(PoolChunk* cur);

      PoolArena* m_arena;
      // 每个chunkList的都有一个上下限：minUsage和maxUsage
      // 两个相邻的chunkList，前一个的maxUsage和后一个的minUsage必须有一段交叉值进行缓冲
      // 否则会出现某个chunk的usage处于临界值，而导致不停的在两个chunk间移动。
      PoolChunkList* m_prevList;
      PoolChunkList* m_nextList;
      int m_minUsage;
      int m_maxUsage;
      int m_maxCapacity;
      PoolChunk* m_head;
      int m_freeMinThreshold;
      int m_freeMaxThreshold;


    public:
      PoolChunkList(PoolArena* arena, PoolChunkList* nextList, int minUsage, int maxUsage, int chunkSize);
      void prevList(PoolChunkList* prevList);
      // chunk的生命周期不会固定在某个chunkList中
      // 随着内存的分配和释放，根据当前的内存使用率，在chunkList链表中前后移动。
      bool allocate(PooledByteBuf* buf, int reqCapacity, int sizeIdx, PoolThreadCache* threadCache);
      bool free(PoolChunk* chunk, long handle, int normCapacity, char* buffer);
      void add(PoolChunk* chunk);
      void add0(PoolChunk* chunk);
      int minUsage();
      int maxUsage();
      static int minUsage0(int value) {return std::max(1, value);}
      // void destroy(PoolArena* arena);
  };
}

#endif // BUFFER_POOLCHUNKLIST_H
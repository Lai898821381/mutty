#ifndef BUFFER_POOLSUBPAGE_H
#define BUFFER_POOLSUBPAGE_H

#include <mutex>
#include <vector>
#include "PoolChunk.h"
#include "SizeClasses.h"

namespace buffer{
  class SizeClasses;
  class PoolArena;
  class PoolChunk;
  class PoolSubpage{
  // PoolSubpage实际上就是PoolChunk中的一个Normal内存块，大小为其管理的内存块size与pageSize最小公倍数。
  // 采用位图管理内存块bitmap
  // 一个PoolSubpage中的内存块size都相同，该size对应sizeClasses#sizeClasses表格中的一个索引index
  // 新建的PoolSubpage都必须加入到PoolArena#smallpagePools[index]链表中
      void addToPool(PoolSubpage* head);
      void removeFromPool();
      void setNextAvail(int bitmapIdx);
      int getNextAvail();
      int findNextAvail();
      int findNextAvail0(int i, long bits);
      long toHandle(int bitmapIdx);
    
      int m_pageShifts;
      int m_runOffset; // 当前PoolSubpage占用的8KB内存在PoolChunk中相对于叶节点的起始点的偏移量
      int m_runSize;
      // 通过位图bitmap记录每个内存块是否已经被使用
      // 用来辅助计算m_nextAvail
      std::vector<long> m_bitmap;
      // 内存页最多能被分配几次 = runSize / elemSize;
      int m_maxNumElems;
      // bitmap使用的long元素个数
      int m_bitmapLength;
      // 内存页内的下一个待分配块的索引
      // Normal分配时，当内存分配成功之后会返回一个handle整形数，通过这个整数型来计算偏移量来确定最终的物理内存
      // 而页内分配除了返回handle整数计算在chunk内的偏移量
      // 同时还需要返回一个bitmapIdx来计算页内的偏移量
      // 通过这两个偏移量来确定最终的物理内存，这个bitmapIdx的值等同于nextAvail。
      int m_nextAvail;
      // 内存页还能被分配几次
      // 当 bitmap 分配成功后，PoolSubpage 会将可用节点的个数 numAvail 减 1
      // 当 numAvail 降为 0 时，表示 PoolSubpage 已经没有可分配的内存块
      // 此时需要从 PoolArena 中 tinySubpagePools 的双向链表中删除。
      int m_numAvail;
    public:
      PoolSubpage();
      PoolSubpage(PoolSubpage* head, PoolChunk* chunk, int pageShifts, int runOffset, int runSize, int elemSize);

      bool free(PoolSubpage* head, int bitmapIdx);
      long allocate();
      int pageSize();
      // void destroy();

      PoolSubpage* prev;
      PoolSubpage* next;

      bool m_doNotDestroy;
      // 每个内存块的大小
      int m_elemSize;
      PoolChunk* m_chunk;
      
      std::mutex m_mtx;
  };
}

#endif // BUFFER_POOLSUBPAGE_H
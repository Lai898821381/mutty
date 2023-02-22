#ifndef BUFFER_SIZECLASS_H
#define BUFFER_SIZECLASS_H

#include <assert.h>
#include <vector>
#include <mutex>
#include <memory>
#include "MathUtil.h"

namespace buffer{
/*   The first size class and spacing are 1 << LOG2_QUANTUM.
 *   Each group has 1 << LOG2_SIZE_CLASS_GROUP of size classes.
 *
 *   size = 1 << log2Group + nDelta * (1 << log2Delta)
 *
 *   The first size class has an unusual encoding, because the size has to be
 *   split between group and delta*nDelta.
 *
 *   If pageShift = 13, sizeClasses looks like this:
 *
 *   (index, log2Group, log2Delta, nDelta, isMultiPageSize, isSubPage, log2DeltaLookup)
 * <p>
 *   ( 0,     4,        4,         0,       no,             yes,        4)
 *   ( 1,     4,        4,         1,       no,             yes,        4)
 *   ( 2,     4,        4,         2,       no,             yes,        4)
 *   ( 3,     4,        4,         3,       no,             yes,        4)
 * <p>
 *   ( 4,     6,        4,         1,       no,             yes,        4)
 *   ( 5,     6,        4,         2,       no,             yes,        4)
 *   ( 6,     6,        4,         3,       no,             yes,        4)
 *   ( 7,     6,        4,         4,       no,             yes,        4)
 * <p>
 *   ( 8,     7,        5,         1,       no,             yes,        5)
 *   ( 9,     7,        5,         2,       no,             yes,        5)
 *   ( 10,    7,        5,         3,       no,             yes,        5)
 *   ( 11,    7,        5,         4,       no,             yes,        5)
 *   ...
 *   ...
 *   ( 72,    23,       21,        1,       yes,            no,        no)
 *   ( 73,    23,       21,        2,       yes,            no,        no)
 *   ( 74,    23,       21,        3,       yes,            no,        no)
 *   ( 75,    23,       21,        4,       yes,            no,        no)
 * <p>
 *   ( 76,    24,       22,        1,       yes,            no,        no)
 * 
 * size总是(1<<log2Delta)的倍数，这个倍数为(nDelta+2^) 每一个分组 nDelta为1-4(第0分组为0-3) 
 * 
 */

  // 处理jemalloc4算法的内存大小分配
  // 为内存池中的内存块提供大小对齐、索引计算等服务方法
  class SizeClasses{
      static const int LOG2_SIZE_CLASS_GROUP = 2;
      static const int LOG2_MAX_LOOKUP_SIZE = 12;

      static const int INDEX_IDX = 0;
      static const int LOG2GROUP_IDX = 1;
      static const int LOG2DELTA_IDX = 2;
      static const int NDELTA_IDX = 3;
      static const int PAGESIZE_IDX = 4;
      static const int SUBPAGE_IDX = 5;
      static const int LOG2_DELTA_LOOKUP_IDX = 6;

      static const unsigned char no = 0, yes = 1;

      int m_lookupMaxSize;
      // 二维数组，一共是76个长度为7的short[]一维数组
      // short[7]存储表格前7标头数据
      // index:内存块size的索引   log2Group:内存块分组，用于计算对应的size
      // log2Delata:增量大小的log2值，用于计算对应的size nDelta:增量乘数，用于计算对应的size
      // isMultipageSize:表示size是否为page的倍数 isSubPage:表示是否为一个subPage类型
      // log2DeltaLookup:如果size存在位图中的，记录其log2Delta，未使用
      // isSubpage为1的size为Small内存块，其他为Normal内存块 Normal内存块必须是page的倍数
      std::vector<std::unique_ptr<unsigned short[]> > m_sizeClasses;

      // 用来记录 pageIdxAndPages 对应的内存规格大小
      // 数组长度是 40，存储了表格size的数据，数组下标就是 pageIdxAndPages
      std::unique_ptr<int[]> m_pageIdx2sizeTab;
      // lookup table for sizeIdx <= smallMaxSizeIdx
      // 记录index对应的内存规格大小
      // 数组长度76，存储了表格size的数据，数据下标就是index
      std::unique_ptr<int[]> m_sizeIdx2sizeTab;
      // lookup table used for size <= lookupMaxclass
      // spacing is 1 << LOG2_QUANTUM, so the size of array is lookupMaxclass >> LOG2_QUANTUM
      // 用来快速计算小内存规格对应的sizeIdx的值，例如66对应3
      // 大小为256，可以快速计算256B以内的内存对应的sizeIdx
      // 保存了(size-1)/(2^LOG2_QUANTUM) --> idx的对应关系。
      // sizeClasses方法中，sizeClasses表格中每个size都是(2^LOG2_QUANTUM) 的倍数。
      std::unique_ptr<int[]> m_size2idxTab;

      int sizeClasses();
      int sizeClass(int index, int log2Group, int log2Delta, int nDelta);
      void idx2SizeTab(int* sizeIdx2sizeTab, int* pageIdx2sizeTab);
      void size2idxTab(int* size2idxTab);
      // directMemoryCacheAlignment > 0时使用
      // int alignSize(int size);
      static int normalizeSizeCompute(int size);
      int pages2pageIdxCompute(int pages, bool floor);

    public:
      SizeClasses(int pageSize, int pageShifts, int chunkSize);
      ~SizeClasses() = default;
      
      // 分配Small内存块，需要找到对应的index，size2idxTab用于计算index
      int size2SizeIdx(int size);
      int sizeIdx2size(int sizeIdx);
      int sizeIdx2sizeCompute(int sizeIdx); // 未使用
      long pageIdx2size(int pageIdx); // 未使用
      // PoolChunk中分配Normal内存块需求查询对应的pageIdx。
      // 通过pages2pageIdxCompute方法计算pageIdx。
      long pageIdx2sizeCompute(int pageIdx); // 未使用
      int pages2pageIdx(int pages);
      int pages2pageIdxFloor(int pages);
      int normalizeSize(int size);

      int m_nSizes;
      int m_nSubpages;
      int m_nPSizes;

      int m_pageSize;
      int m_pageShifts;
      // 由allocator创建arena时设置，通过validateAndCalculateChunkSize(pageSize, maxOrder)计算得到
      int m_chunkSize;
      int m_smallMaxSizeIdx;

      static const int LOG2_QUANTUM = 4;
  };
}

#endif // BUFFER_SIZECLASS_H
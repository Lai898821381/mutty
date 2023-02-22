#ifndef BUFFER_POOLCHUNK_H
#define BUFFER_POOLCHUNK_H

#include <unordered_map>
#include <deque>
#include <mutex>
#include "LongPriorityQueue.h"
#include "PoolArena.h"
#include "PoolChunkList.h"
#include "PoolThreadCache.h"
#include "PooledByteBuf.h"
#include "PooledByteBufAllocator.h"

namespace buffer{
    class LongPriorityQueue;
    class PoolArena;
    class PoolChunkList;
    class PooledByteBuf;
    class PooledByteBufAllocator;
    class PoolSubpage;
    class PoolThreadCache;
    class PoolChunk{
/*
 *     /-----------------\
 *     | run             |
 *     |                 |
 *     |                 |
 *     |-----------------|
 *     | run             |
 *     |                 |
 *     |-----------------|
 *     | unalloctated    |
 *     | (freed)         |
 *     |                 |
 *     |-----------------|
 *     | subpage         |
 *     |-----------------|
 *     | unallocated     |
 *     | (freed)         |
 *     | ...             |
 *     | ...             |
 *     | ...             |
 *     |                 |
 *     |                 |
 *     |                 |
 *     \-----------------/
 * 
 * run: 一个 run 中包含多个 page；也可以只包含一个 page，就是单page 的 run。
 * chunk: 就是表示这个PoolChunk，包含多个 run 。
 * 用一个long类型的handle来表示不同的run
 */

            static const int SIZE_BIT_LENGTH = 15;
            static const int INUSED_BIT_LENGTH = 1;
            static const int SUBPAGE_BIT_LENGTH = 1;
            static const int BITMAP_IDX_BIT_LENGTH = 32;
            // key:runOffset value:handle
            // 存放着所有可分配的run内存块第一个和最后一个页偏移量runOffset，以及run内存块的handle
            // 用于释放内存块时，合并相邻内存块
            // 通过insertAvailRun0赋值
            std::unordered_map<int, long> m_runsAvailMap;
            // 1 handle:
            // long handle表示不同的run
            // oooooooo ooooooos ssssssss ssssssue bbbbbbbb bbbbbbbb bbbbbbbb bbbbbbbb
            // o：前十五位bit 表示这个内存块run 的页偏移量 runOffset。
            // 因为 PoolChunk 是按照页来进行分割的
            // 页偏移量 runOffset表示这个内存块run 在这个 PoolChunk 第runOffset 页的位置。
            // s：中间十五位bit 表示这个内存块run 拥有的页数size。
            // u：一位bit 表示这个内存块run 是否被使用。
            // e：一位bit 表示这个内存块run 是否是 isSubpage。
            // b: 一共32位，表示subpage 的 bitmapIdx。内存块在subpage中的索引

            // 优先级队列数组，数组长度为【pageIdx】的大小，默认为40
            // 【管理所有可分配的run内存块】，run根据拥有的页数不同可以分为40种
            // runsAvail中每一个优先级队列管理一种类型run的内存块
            // 存放的是handle（维护内存块的信息）
            // 【排序】的是run内存块的【页偏移量runOffset】
            // 初始时，runsAvail 在下标 39 的优先级队列中存放这个整块PoolChunk 的 run 内存块
            // 即 runOffset 是 0, size 是 chunkSize/pageSize。
            // 通过insertAvailRun赋值 ->offer
            std::vector<LongPriorityQueue*> m_runsAvail;
            // 用于管理PoolChunk下的所有PoolSubpage
            // 对应二叉树中2048个节点
            // 每一个PoolSubPage代表了二叉树的一个叶节点
            // 当二叉树叶节点内存被分配之后，其会使用一个PoolSubPage对其进行封装
            std::vector<PoolSubpage*> m_subpages;
            // 对创建的ByteBuffer进行缓存的一个队列
            std::deque<char*> m_cachedBuffers; // cachedNioBuffers
            std::mutex m_runAvailMtx;

            int m_pageSize;
            int m_pageShifts;
            int m_chunkSize;

            std::vector<LongPriorityQueue*> newRunsAvailqueueArray(int size);
            // std::vector<LongPriorityQueue*> newRunsAvailQueue(int size);
            void insertAvailRun(int runOffset, int pages, long handle);
            inline static int lastPage(int runOffset, int pages) {
                return runOffset + pages - 1;
            }
            void insertAvailRun0(int runOffset, long handle);
            long allocateRun(int runSize);
            int calculateRunSize(int sizeIdx);
            int runFirstBestFit(int pageIdx);
            long splitLargeRun(long handle, int needPages);
            static long toRunHandle(int runOffset, int runPages, int inUsed);
            long allocateSubpage(int sizeIdx);
            long collapseRuns(long handle) {
                return collapseNext(collapsePast(handle)); // 不断向前和向后合并可用内存块
            }
            long collapsePast(long handle); // 合并前面的可用内存块
            long collapseNext(long handle); // 合并后面的可用内存块
            void removeAvailRun(long handle);
            void removeAvailRun(LongPriorityQueue* queue, long handle);
            // int runSize(int pageShifts, long handle);
            long getAvailRunByOffset(int runOffset);

        public:
            static const int IS_SUBPAGE_SHIFT = BITMAP_IDX_BIT_LENGTH;
            static const int IS_USED_SHIFT = SUBPAGE_BIT_LENGTH + IS_SUBPAGE_SHIFT;
            static const int SIZE_SHIFT = INUSED_BIT_LENGTH + IS_USED_SHIFT;
            static const int RUN_OFFSET_SHIFT = SIZE_BIT_LENGTH + SIZE_SHIFT;

            PoolChunk(PoolArena* arena, const char* memory, int pageSize, int pageShifts, int chunkSize, int maxPageIdx);
            PoolChunk(PoolArena* arena, const char* memory, int size);
            ~PoolChunk();
            void free(long handle, int normCapacity, const char* buffer);
            void initBuf(PooledByteBuf* buf, const char* buffer, long handle, int reqCapacity, PoolThreadCache* threadCache);
            void initBufWithSubpage(PooledByteBuf* buf, const char* buffer, long handle, int reqCapacity, PoolThreadCache* threadCache);            
            static int calculateRunOffset(long handle) {
                return static_cast<int>(handle >> RUN_OFFSET_SHIFT);
            }
            static int calculateRunSize(int pageShifts, long handle) {
                return calculateRunPages(handle) << pageShifts;
            }
            static int calculateRunPages(long handle) {
                return static_cast<int>(handle >> SIZE_SHIFT & 0x7fff);
            }
            static bool isUsed(long handle) {
                return (handle >> IS_USED_SHIFT & 1) == 1L;
            }
            static bool isRun(long handle) {
                return !isSubpage(handle);
            }
            static bool isSubpage(long handle) {
                return (handle >> IS_SUBPAGE_SHIFT & 1) == 1L;
            }
            static int calculateBitmapIdx(long handle) {
                return static_cast<int>(handle);
            }

            int chunkSize(){
                return m_chunkSize;
            }
            bool allocate(PooledByteBuf* buf, int reqCapacity, int sizeIdx, PoolThreadCache* cache);
            int usage();
            // void destroy(){}

            
            PoolArena* m_arena; // PoolChunk所属Arena
            // 当前申请的内存块，16M
            char* m_memory;
            int m_freeBytes; // 记录了当前PoolChunk中还剩余的可申请的字节数
            bool m_unpooled; // 指定当前是否使用内存池的方式进行管理
            
            PoolChunk* prev;
            PoolChunk* next;
            PoolChunkList* m_parent;
    };
}

#endif // BUFFER_POOLCHUNK_H
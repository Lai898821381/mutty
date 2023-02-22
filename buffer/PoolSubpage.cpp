#include <assert.h>
#include <limits.h>
#include "PoolSubpage.h"

namespace buffer{
    PoolSubpage::PoolSubpage()
    :m_maxNumElems(0), m_bitmapLength(0), m_nextAvail(0), m_numAvail(0), prev(nullptr), next(nullptr), m_doNotDestroy(true){
        m_chunk = nullptr;
        m_pageShifts = -1;
        m_runOffset = -1;
        m_elemSize = -1;
        m_runSize = -1;
    }
    // 3.1.1初始化
    // 内存池初始化时内存页缓存smallSubpagePools页是空的
    // 在PoolSubpage初始化之后会自动把它添加到smallSubpagePools的缓存中
    PoolSubpage::PoolSubpage(PoolSubpage* head, PoolChunk* chunk,
                            int pageShifts, int runOffset, int runSize, int elemSize)
    :prev(nullptr), next(nullptr),
    m_chunk(chunk),
    m_pageShifts(pageShifts),
    m_runOffset(runOffset),
    m_runSize(runSize),
    m_elemSize(elemSize){      
        // 创建bitmap位图数组
        // 初始化bitmap长度为8，分配内存大小最小为16，一个Page可以分为8192/16=512个内存段
        // 一个long有64位，可以描述64个内存段，只需要512/64=8个long就可以描述全部内存段
        m_bitmap.assign(runSize >> 6 + SizeClasses::LOG2_QUANTUM, 0L); // runSize / 64 / QUANTUM
        // 当前这 PoolSubpage 只会分配 elemSize 大小容量的内存
        m_doNotDestroy = true;
        if (elemSize != 0) {
            // PoolSubpage 一共可以分配多少块这个容量的内存
            // 如分配4096大小内存，则maxNumElems=numAvail=8192/4096=2
            // 如分配32大小内存，则maxNumElems=numAvail=8192/32=256
            m_maxNumElems = m_numAvail = runSize / elemSize;
            m_nextAvail = 0;
            // 无符号右移6位，也就是除以64，因为一个 long 有64个二进制位
            // 如分配4096大小内存，则2>>6=0,2&63!=0,bitmapLength=1，说明只需要一个long就可以描述两个内存段状态
            // 如分配32大小内存，则256>>6=4,256&63==0,bitmapLength=4，说明需要4个long描述256个内存段状态
            m_bitmapLength = m_maxNumElems >> 6;
            // maxNumElems不能整除64，所以bitmapLength要加1，用于管理余下的内存块。
            if ((m_maxNumElems & 63) != 0) {
                m_bitmapLength ++;
            }

            for (int i = 0; i < m_bitmapLength; i ++) {
                m_bitmap[i] = 0;
            }
        }
        // 添加到 PoolArena 中对应尺寸容量的PoolSubpage链表中
        // 将这个PoolSubpage添加到PoolArena中对应尺寸容量的PoolSubpage链表中
        // 这样就不需要需要查找，加快内存块分配速度。
        addToPool(head);
    }
    // 3.1.2分配内存块
    /**
     * 返回子页面内存分配的位图索引
     * 使用 long 类型每个二进制位数`0`或 `1` 来记录这块内存有没有被分配过，
     * 因为 long 是8个字节，64位二进制数，所以可以表示 64 个内存块分配情况。
     */
    long PoolSubpage::allocate(){
        // 没有可用内存块
        if (m_numAvail == 0 || !m_doNotDestroy) {
            return -1;
        }

        // 得到下一个可用的位图 bitmap 索引
        // findNextAvail
        int bitmapIdx = getNextAvail();
        // 获取该内存块在bitmap数组中第q元素 除以 64 得到的整数，即 bitmap[] 数组的下标
        int q = bitmapIdx >> 6;
        // 获取该内存块是bitmap数组中第q个元素的第r个bit位
        // 与 64 的余数，即占据的 long 类型的位数
        int r = bitmapIdx & 63;
        // 必须是 0， 表示这一块内存没有被分配
        assert((m_bitmap[q] >> r & 1) == 0);
        // 将bitmap数组中第q个元素的第r个bit位设置为1，表示已经使用
        m_bitmap[q] |= 1L << r;
        // 所有内存块已分配了，则将其从PoolArena中移除。
        if (-- m_numAvail == 0) {
            // 可分配内存块的数量numAvail为0，
            removeFromPool();
        }
        // 使用long类型的高32位储存 bitmapIdx 的值，即使用 PoolSubpage 中那一块的内存；
        // 低32位储存 memoryMapIdx 的值，即表示使用那一个 PoolSubpage
        // 返回Handle值 toHandle 转换为最终的handle
        return toHandle(bitmapIdx);
    }
    // 3.1.3回收内存块
    // 返回 true，表示这个 PoolSubpage 还在使用，即上面还有其他小内存块被使用；
    // 返回 false，表示这个 PoolSubpage 上面分配的小内存块都释放了，可以回收整个 PoolSubpage。
    bool PoolSubpage::free(PoolSubpage* head, int bitmapIdx){
        if (m_elemSize == 0) return true;
        // 见allocate
        int q = bitmapIdx >> 6;
        int r = bitmapIdx & 63;
        // 必须不能是 0， 表示这个 bitmapIdx 对应内存块肯定是在被使用
        assert((m_bitmap[q] >> r & 1) != 0);
        m_bitmap[q] ^= 1L << r;
        // 将 bitmapIdx 设置为下一个可以使用的内存块索引，
        // 因为刚被释放，这样就不用进行搜索来查找可用内存块索引。
        setNextAvail(bitmapIdx);
        // 在PoolSubpage的内存块全部被使用时，释放了某个内存块，这时重新加入到PoolArena中。
        if (m_numAvail ++ == 0) {
            // 如果可分配内存块的数量numAvail从0开始增加，
            // 那么就要重新添加到 PoolArena 中对应尺寸容量的PoolSubpage链表中
            addToPool(head);
            if (m_maxNumElems > 1) {
                return true;
            }
        }
        // 未完全释放，即还存在已分配内存块，返回true
        if (m_numAvail != m_maxNumElems) {
            return true;
        } else {
            // 处理所有内存块已经完全释放的场景
            // PoolArena#smallSubpagePools链表组成双向链表
            // 链表中只有head和当前PoolSubpage时，当前PoolSubpage的prev，next都指向head。
            if (prev == next) {
                // 这时当前​PoolSubpage是PoolArena中该链表最后一个PoolSubpage，不释放该PoolSubpage
                // 以便下次申请内存时直接从该PoolSubpage上分配
                return true;
            }
            // 如果 prev != next，即 subpage 组成的链表中还有其他 subpage，那么就删除它
            m_doNotDestroy = false;
            removeFromPool();
            // 返回false PoolChunk会释放对应page节点
            return false;
        }
    }

    void PoolSubpage::addToPool(PoolSubpage* head){
        assert(prev == nullptr && next == nullptr);
        prev = head;
        next = head->next;
        next->prev = this;
        head->next = this;
    }

    void PoolSubpage::removeFromPool(){
        assert(prev != nullptr && next != nullptr);
        prev->next = next;
        next->prev = prev;
        next = nullptr;
        prev = nullptr;
    }

    void PoolSubpage::setNextAvail(int bitmapIdx){
        m_nextAvail = bitmapIdx;
    }

    int PoolSubpage::getNextAvail(){
        // nextAvail为初始值或free时释放的值。
        int nextAvail = m_nextAvail;
        // 如果nextAvail大于等于0，说明nextAvail指向了下一个可分配的内存段，直接返回nextAvail值；
        // 每次分配完成，nextAvail被置为-1，这时只能通过方法findNextAvail重新计算出下一个可分配的内存段位置。
        if (nextAvail >= 0) {
            m_nextAvail = -1;
            return nextAvail;
        }
        return findNextAvail();
    }

    int PoolSubpage::findNextAvail(){
        int bitmapLength = m_bitmapLength;
        // 从memoryMap的第一个元素（索引1的元素）开始检查
        for (int i = 0; i < m_bitmapLength; i ++) {
            long bits = m_bitmap[i];
            // 说明这个long描述的64个内存段还有未分配的
            if (~bits != 0) {
                return findNextAvail0(i, bits);
            }
        }
        return -1;
    }

    int PoolSubpage::findNextAvail0(int i, long bits){
        int baseVal = i << 6;
        // 从左到右遍历值为0的位置
        for (int j = 0; j < 64; j ++) {
            // 用来判断该位置是否被分配
            if ((bits & 1) == 0) {
                int val = baseVal | j;
                if (val < m_maxNumElems) {
                    return val;
                } else {
                    break;
                }
            }
            // 否则bits右移一位
            bits >>= 1;
        }
        return -1;
    }

    long PoolSubpage::toHandle(int bitmapIdx){
        // 虽然我们使用高 32 为表示 bitmapIdx，但是当bitmapIdx = 0 时，
        // 就无法确定是否表示 bitmapIdx 的值。
        // 所以这里就 0x4000000000000000L | (long) bitmapIdx << 32，那进行区分。
        // 放心  bitmapIdx << 32 是不可能超过 0x4000000000000000L
        // return 0x4000000000000000L | (long) bitmapIdx << 32 | memoryMapIdx;
        int pages = m_runSize >> m_pageShifts;
        return static_cast<long> (m_runOffset) << PoolChunk::RUN_OFFSET_SHIFT
                | static_cast<long> (pages) << PoolChunk::SIZE_SHIFT
                | 1L << PoolChunk::IS_USED_SHIFT
                | 1L << PoolChunk::IS_SUBPAGE_SHIFT
                | bitmapIdx;
    }

    int PoolSubpage::pageSize(){
        return 1 << m_pageShifts;
    }

    // void PoolSubpage::destroy()
    // {
    //     if (m_chunk != nullptr) {
    //         m_chunk->destroy();
    //     }
    // }
}
#include "PoolChunkList.h"

namespace buffer{
    PoolChunkList::PoolChunkList(PoolArena* arena, PoolChunkList* nextList,
                                int minUsage, int maxUsage, int chunkSize)
    :m_prevList(nullptr), m_head(nullptr),
    m_arena(arena),
    m_nextList(nextList),
    m_minUsage(minUsage),
    m_maxUsage(maxUsage){
        assert(minUsage <= maxUsage);
        m_maxCapacity = calculateMaxCapacity(minUsage, chunkSize);

        m_freeMinThreshold = (maxUsage == 100) ? 0 : static_cast<int>(chunkSize * (100.0 - m_maxUsage + 0.99999999) / 100L);
        m_freeMaxThreshold = (minUsage == 100) ? 0 : static_cast<int>(chunkSize * (100.0 - m_minUsage + 0.99999999) / 100L);
    }

    int PoolChunkList::calculateMaxCapacity(int minUsage, int chunkSize){
        minUsage = std::max(0, minUsage);
        if(minUsage == 100) return 0;
        return static_cast<int> (chunkSize * (100L - minUsage) / 100L);
    }
    
    void PoolChunkList::prevList(PoolChunkList* prevList){       
        assert(m_prevList == nullptr);
        m_prevList = prevList;
    }
    /* 随着chunk中page的不断分配和释放，会导致很多碎片内存段
    大大增加了之后分配一段连续内存的失败率
    针对这种情况，可以把内存使用率较大的chunk放到PoolChunkList链表更后面*/
    bool PoolChunkList::allocate(PooledByteBuf* buf, int reqCapacity, int sizeIdx, PoolThreadCache* threadCache){
        int normCapacity = m_arena->sizeIdx2size(sizeIdx);
        if (normCapacity > m_maxCapacity) {
            return false;
        }
        for (PoolChunk* cur = m_head; cur != nullptr; cur = cur->next) {
            if (cur->allocate(buf, reqCapacity, sizeIdx, threadCache)) {
                // 假设poolChunkList中已经存在多个chunk。当分配完内存后
                // 如果当前chunk的使用量超过maxUsage
                // 则把该chunk从当前链表中删除，添加到下一个链表中。
                if (cur->m_freeBytes <= m_freeMinThreshold) {
                    remove(cur);
                    m_nextList->add(cur);
                }
                return true;
            }
        }
        return false;
    }
    
    bool PoolChunkList::free(PoolChunk* chunk, long handle, int normCapacity, char* buffer){
        chunk->free(handle, normCapacity, buffer);
        // 随着chunk中内存的释放，其内存使用率也会随着下降
        // 当下降到freeMaxThreshold时，该chunk会移动到前一个列表中
        if (chunk->m_freeBytes > m_freeMaxThreshold) {
            remove(chunk);
            // Move the PoolChunk down the PoolChunkList linked-list.
            return move0(chunk);
        }
        return true;
    }

    // move : from high-used  to low-used
    // add : from low-used to high-used
    bool PoolChunkList::move(PoolChunk* chunk){
        assert(chunk->usage() < m_maxUsage);

        if (chunk->m_freeBytes > m_freeMaxThreshold) {
            // Move the PoolChunk down the PoolChunkList linked-list.
            return move0(chunk);
        }

        // PoolChunk fits into this PoolChunkList, adding it here.
        add0(chunk);
        return true;
    }

    bool PoolChunkList::move0(PoolChunk* chunk){
        if (m_prevList == nullptr) {
            // There is no previous PoolChunkList so return false which result in having the PoolChunk destroyed and
            // all memory associated with the PoolChunk will be released.
            assert(chunk->usage() == 0);
            return false;
        }
        return m_prevList->move(chunk);
    }
    
    void PoolChunkList::add(PoolChunk* chunk){
        if (chunk->m_freeBytes <= m_freeMinThreshold) {
            m_nextList->add(chunk);
            return;
        }
        add0(chunk);
    }

    void PoolChunkList::add0(PoolChunk* chunk){
        chunk->m_parent = this;
        if (m_head == nullptr) {
            m_head = chunk;
            chunk->prev = nullptr;
            chunk->next = nullptr;
        } else {
            chunk->prev = nullptr;
            chunk->next = m_head;
            m_head->prev = chunk;
            m_head = chunk;
        }
    }
    void PoolChunkList::remove(PoolChunk* cur)
    {
        if (cur == m_head) {
            m_head = cur->next;
            if (m_head != nullptr) {
                m_head->prev = nullptr;
            }
        } else {
            PoolChunk* next = cur->next;
            cur->prev->next = next;
            if (next != nullptr) {
                next->prev = cur->prev;
            }
        }
    }
    // void PoolChunkList::destroy(PoolArena* arena)
    // {
    //     PoolChunk* chunk = head;
    //     while (chunk != nullptr) {
    //         m_arena->destroyChunk(chunk);
    //         chunk = chunk->next;
    //     }
    //     head = nullptr;
    // }

    int PoolChunkList::minUsage(){
        return std::max(1, m_minUsage);
    }

    int PoolChunkList::maxUsage(){
        return std::min(m_maxUsage, 100);
    }
}
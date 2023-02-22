#include "PoolChunk.h"

namespace buffer{  
    PoolChunk::PoolChunk(PoolArena* arena, const char* memory, int pageSize,
                        int pageShifts, int chunkSize, int maxPageIdx)
    :m_unpooled(false),
    m_arena(arena),
    m_memory(const_cast<char*>(memory)),
    m_pageSize(pageSize),
    m_pageShifts(pageShifts),
    m_chunkSize(chunkSize),
    prev(nullptr),
    next(nullptr),
    m_parent(nullptr) {
        m_freeBytes = chunkSize;
        m_runsAvail = std::move(newRunsAvailqueueArray(maxPageIdx));
        m_subpages.assign(chunkSize >> pageShifts, nullptr);

        // 在runsAvail数组最后位置插入一个handle，该handle代表page偏移位置为0的地方可以分配16M的内存块
        int pages = m_chunkSize >> m_pageShifts;
        long initHandle = static_cast<long>(pages) << SIZE_SHIFT;
        insertAvailRun(0, pages, initHandle);
    }
    
    PoolChunk::PoolChunk(PoolArena* arena, const char* memory, int size)
    :m_unpooled(true),
    m_arena(arena),
    m_memory(const_cast<char*>(memory)),
    m_pageSize(0),
    m_pageShifts(0),
    m_chunkSize(size),
    m_freeBytes(0), m_parent(nullptr), prev(nullptr), next(nullptr)
    {}

    PoolChunk::~PoolChunk(){
        if(m_memory != nullptr){
            delete[] m_memory;
            m_memory = nullptr;
        }

        for(LongPriorityQueue* ptr : m_runsAvail){
            if(ptr != nullptr) delete ptr;
        }
        for(PoolSubpage* ptr : m_subpages){
            if(ptr != nullptr) delete ptr;
        }     

    }

    std::vector<LongPriorityQueue*> PoolChunk::newRunsAvailqueueArray(int size){
        std::vector<LongPriorityQueue*> runsAvail(size);
        for (int i = 0; i < size; i++) {
            runsAvail[i] = new LongPriorityQueue();
        }
        return runsAvail;
    }

    void PoolChunk::insertAvailRun(int runOffset, int pages, long handle){
        int pageIdxFloor = m_arena->pages2pageIdxFloor(pages);
        m_runsAvail[pageIdxFloor]->offer(handle);

        //insert first page of run
        insertAvailRun0(runOffset, handle);
        if (pages > 1) {
            //insert last page of run
            insertAvailRun0(lastPage(runOffset, pages), handle);
        }
    }

    void PoolChunk::insertAvailRun0(int runOffset, long handle){
        m_runsAvailMap.insert(std::make_pair(runOffset,handle));
    }

    void PoolChunk::removeAvailRun(long handle){
        int pageIdxFloor = m_arena->pages2pageIdxFloor(calculateRunPages(handle));
        removeAvailRun(m_runsAvail[pageIdxFloor], handle);
    }

    void PoolChunk::removeAvailRun(LongPriorityQueue* queue, long handle){
        queue->remove(handle);

        int m_runOffset = calculateRunOffset(handle);
        int pages = calculateRunPages(handle);
        //remove first page of run
        m_runsAvailMap.erase(m_runOffset);
        if (pages > 1) {
            //remove last page of run
            m_runsAvailMap.erase(lastPage(m_runOffset, pages));
        }
    }

    int PoolChunk::usage(){
        if (m_freeBytes == 0) return 100;
        int freePercentage = static_cast<int> (m_freeBytes * 100L / m_chunkSize);
        if (freePercentage == 0)  return 99;
        return 100 - freePercentage;
    }
    
    // 判断其是否大于8KB，如果大于8KB，则会直接在PoolChunk的二叉树中年进行分配
    // 如果小于8KB，则会直接申请一个8KB的内存，然后将8KB的内存交由一个PoolSubpage进行维护。
    bool PoolChunk::allocate(PooledByteBuf* buf, int reqCapacity, int sizeIdx, PoolThreadCache* cache){
        // 低位的4个字节表示当前normCapacity分配的内存在PoolChunk中所分配的节点在整个memoryMap数组中的下标索引
        // 高位的4个字节则表示当前需要分配的内存在PoolSubPage所代表的8KB内存中的位图索引。
        // 对于大于8KB的内存分配，由于其不会使用PoolSubPage来存储目标内存，高位四个字节的位图索引为0，
        // 而低位的4个字节则还是表示目标内存节点在memoryMap中的位置索引；
        // 对于低于8KB的内存分配，其会使用一个PoolSubPage来表示整个8KB内存，需要一个位图索引来表示目标内存
        // 也即normCapacity会占用PoolSubPage中的哪一部分的内存。
        long handle;
        if (sizeIdx <= m_arena->m_smallMaxSizeIdx) {
            // small
            // 分配small内存块两个步骤：
            // 1、PoolChunk中分配PoolSubpage，如果PoolArena#smallSubpagePools中已经有对应的PoolSubpage缓冲，则不需要该步骤。
            // 2、PoolSubpage上分配内存块
            handle = allocateSubpage(sizeIdx);
            if (handle < 0) {
                return false;
            }
            assert(isSubpage(handle));
        } else {
            // normal
            // runSize must be multiple of pageSize 根据内存块索引查找对应内存块size
            int runSize = m_arena->sizeIdx2size(sizeIdx);
            handle = allocateRun(runSize);
            if (handle < 0) {
                return false;
            }
        }
        // 这里会从缓存的ByteBuf对象池中获取一个ByteBuf对象，不存在则返回null
        char* buffer = m_cachedBuffers.empty() ? nullptr : m_cachedBuffers.back();
        if(!m_cachedBuffers.empty())
            m_cachedBuffers.pop_back();
        // 通过申请到的内存数据handle和buffer对获取到的ByteBuf对象进行初始化，如果ByteBuf为null，则创建一个新的然后进行初始化
        initBuf(buf, buffer, handle, reqCapacity, cache);
        return true;
    }

    /** 负责分配Normal内存块
     * @return handle：存储了分配的内存块大小和偏移量
     */
    long PoolChunk::allocateRun(int runSize){
        int pages = runSize >> m_pageShifts; // 计算所需pages数量
        int pageIdx = m_arena->pages2pageIdx(pages); // 计算对应pageIdx
        // {
        std::unique_lock<std::mutex> lock(m_runAvailMtx);
        //find first queue which has at least one big enough run
        // #3 从pageIdx开始遍历runsAvail，找到第一个handle，该handle可以分配所需内存块
        int queueIdx = runFirstBestFit(pageIdx);
        if (queueIdx == -1) return -1;

        //get run with min offset in this queue
        LongPriorityQueue* queue = m_runsAvail[queueIdx];
        // 获取这个能分配到的内存块 从runsAvail中删除了这个可用内存块
        assert(!queue->isEmpty());
        long handle = queue->poll();

        // assert(handle != LongPriorityQueue::NO_VALUE && !isUsed(handle));
        // assert(handle != LongPriorityQueue::NO_VALUE);
        assert(!isUsed(handle));
        // 从runsAvailMap移除该handle信息
        removeAvailRun(queue, handle);

        if (handle != -1) {
            // 从#3找到的handle上划分所要的内存块
            // 将这个可用内存块分割成两部分：
            // 一块是 pages 大小，返回给调用者使用。
            // 一块是剩余大小，表示还能分配内存块，还需要存入 runsAvail 中
            handle = splitLargeRun(handle, pages);
        }
        // 减少可用内存字节数
        m_freeBytes -= calculateRunSize(m_pageShifts, handle);
        return handle;
        // }
    }
    // 计算得到 runSize 必须是 pageSize 的倍数
    int PoolChunk::calculateRunSize(int sizeIdx){
        int maxElements = 1 << m_pageShifts - SizeClasses::LOG2_QUANTUM; // 512
        int runSize = 0;
        int nElements;
        // 得到 sizeIdx 对应规格化内存块的大小
        int elemSize = m_arena->sizeIdx2size(sizeIdx);

        //find lowest common multiple of pageSize and elemSize
        // 找到最小 pageSize 的倍数 runSize，能够整除 elemSize
        // runSize != (runSize / elemSize) * elemSize 就表示 runSize 能够整除 elemSize
        do {
            runSize += m_pageSize;
            nElements = runSize / elemSize;
        } while (nElements < maxElements && runSize != nElements * elemSize);

        while (nElements > maxElements) {
            runSize -= m_pageSize;
            nElements = runSize / elemSize;
        }

        assert(nElements > 0);
        assert(runSize <= m_chunkSize);
        assert(runSize >= elemSize);

        return runSize;
    }
    
    /**
     * 根据 pageIdx 寻找第一个足够大规格的 run
     */
    int PoolChunk::runFirstBestFit(int pageIdx){
        if (m_freeBytes == m_chunkSize) {
            // 如果这个 PoolChunk 还没有进行分配，直接返回
            return m_arena->m_nPSizes - 1;
        }
        // 从刚满足run规格的 pageIdx 开始，一直遍历到最大run 规格(arena.nPSizes -1)
        // 从 PoolChunk 中寻找能分配的 run
        for (int i = pageIdx; i < m_arena->m_nPSizes; i++) {
            LongPriorityQueue* queue = m_runsAvail[i];
            if (queue != nullptr && !queue->isEmpty()) {
                return i;
            }
        }
        return -1;
    }

    long PoolChunk::splitLargeRun(long handle, int needPages){
        assert(needPages > 0);
        // 从handle中获取这个可用内存块 run 一共有多少个 pages
        int totalPages = calculateRunPages(handle);
        assert(needPages <= totalPages);
        // 分配后剩余字节数
        int remPages = totalPages - needPages;
        if (remPages > 0) {
            int runOffset = calculateRunOffset(handle);

            // keep track of trailing unused pages for later use
            // runOffset + needPages 表示剩余可用内存块的偏移量
            int availOffset = runOffset + needPages;
            // 得到新的剩余可用的内存块 run 的handle
            long availRun = toRunHandle(availOffset, remPages, 0);
            // 将availRun插入到runsAvail，runsAvailMap中
            insertAvailRun(availOffset, remPages, availRun);

            // not avail
            return toRunHandle(runOffset, needPages, 1);
        }

        //mark it as used
        // 将这个 handle 变成已使用
        handle |= 1L << IS_USED_SHIFT;
        return handle;
    }
    /**
     * Create / initialize a new PoolSubpage of normCapacity. Any PoolSubpage created / initialized here is added to
     * subpage pool in the PoolArena that owns this PoolChunk
     *
     * @param sizeIdx sizeIdx of normalized size
     *
     * @return index in memoryMap
     */
    long PoolChunk::allocateSubpage(int sizeIdx){
        // 涉及修改PoolArena#smallSubpagePools中的PoolSubpage链表，需要同步操作
        PoolSubpage* head = m_arena->findSubpagePoolHead(sizeIdx);
        {
            std::lock_guard<std::mutex> lock(head->m_mtx);
            //allocate a new run
            // 计算内存块size和pageSize最小公倍数 runSize必须是pageSize的整数倍
            int runSize = calculateRunSize(sizeIdx);
            // 分配一个Normal内存块，作为PoolSubpage的底层内存块
            // 分配run内存块
            long runHandle = allocateRun(runSize);
            if (runHandle < 0) {
                return -1;
            }
            // 构建PoolSubpage
            int runOffset = calculateRunOffset(runHandle); // Normal内存块偏移量，也是该PoolSubpage在整个Chunk中的偏移量
            assert(m_subpages[runOffset] == nullptr);
            int elemSize = m_arena->sizeIdx2size(sizeIdx); // Small内存块size
            // 如果subpage为空，则进行初始化，并加入到PoolSubpage数组
            // 创建PoolSubpage对象，分配需求的Small类型内存块
            PoolSubpage* subpage = new PoolSubpage(head, this, m_pageShifts, runOffset,
                                calculateRunSize(m_pageShifts, runHandle), elemSize);

            m_subpages[runOffset] = subpage;
            // 得到PoolSubpage对象后，内存块都是固定大小的，因此直接allocate()返回handle即可分配内存
            return subpage->allocate(); // 在subpage上分配内存块
        }
    }
    
    void PoolChunk::free(long handle, int normCapacity, const char* buffer){
        // 如果是 small 类型，那么调用 subpage.free() 方法进行释放。
        // 释放normal 类型或者 small 类型已经完成释放了，那么就要释放这个 run 内存块。
        // 调用 collapseRuns(handle) 方法来合并可分配内存块。
        // 调用 insertAvailRun(...) 方法将内存块插入到 runsAvail 中。
        if (isSubpage(handle)) {
            int sizeIdx = m_arena->size2SizeIdx(normCapacity);
            // 查找head节点，同步
            // 找到 PoolArena 中这个small 规格类型对应 PoolSubpage 链表
            PoolSubpage* head = m_arena->findSubpagePoolHead(sizeIdx);
            // 从 handle 中得到这个内存块 run 的偏移量 runOffset
            int sIdx = calculateRunOffset(handle);
            // 通过这个偏移量 runOffset 得到对应的 PoolSubpage
            PoolSubpage* subpage = m_subpages[sIdx];
            assert(subpage != nullptr && subpage->m_doNotDestroy);

            // Obtain the head of the PoolSubPage pool that is owned by the PoolArena and synchronize on it.
            // This is need as we may add it back and so alter the linked-list structure.
            {
                std::lock_guard<std::mutex> lock(head->m_mtx);
                // 调用subpage#free释放Small内存块
                // 如果subpage#free返回false，将继续向下执行，这时会释放PoolSubpage整个内存块
                // 否则，不释放PoolSubpage内存块。
                if (subpage->free(head, calculateBitmapIdx(handle))) {
                    //the subpage is still used, do not free it
                    return;
                }
                assert(!subpage->m_doNotDestroy);
                // Null out slot in the array as it was freed and we should not use it anymore.
                // subpage分配的内存块不复用？
                m_subpages[sIdx] = nullptr;
            }
        }
        // 到这里要么是释放normal块，要么是subpage已经被释放完了，都会得到一个完整的run
        // 计算释放的page数
        int pages = calculateRunPages(handle);
        {
            std::lock_guard<std::mutex> lock(m_runAvailMtx);
            // collapse continuous runs, successfully collapsed runs
            // will be removed from runsAvail and runsAvailMap
            // 如果可以，将前后可用内存块合并
            long finalRun = collapseRuns(handle);

            //set run as not used 设置成未使用
            // 插入新的handle
            finalRun &= ~(1L << IS_USED_SHIFT);
            //if it is a subpage, set it to run 设置成非 subpage
            finalRun &= ~(1L << IS_SUBPAGE_SHIFT);
            // 将这个可用内存块run 插入到 runsAvail 中
            insertAvailRun(calculateRunOffset(finalRun), calculateRunPages(finalRun), finalRun);
            // 增加当前 PoolChunk 的可用字节数
            // 虽然可能合并了一个大的可用内存块，但是被合并的内存块原来就是可用的。
            m_freeBytes += pages << m_pageShifts; // 注意，这里一定不能用 runPages(finalRun)，
        }

        if (buffer != nullptr && !m_cachedBuffers.empty() &&
            m_cachedBuffers.size() < PooledByteBufAllocator::DEFAULT_MAX_CACHED_BYTEBUFFERS_PER_CHUNK) {
            m_cachedBuffers.push_back(const_cast<char*>(buffer));
        }
    }

    long PoolChunk::getAvailRunByOffset(int runOffset) {
        if(m_runsAvailMap.find(runOffset) != m_runsAvailMap.end())
            return m_runsAvailMap[runOffset];
        return -1;
    }

    long PoolChunk::collapsePast(long handle){
        // 不断向前和向后合并可用内存块
        for (;;) {
            int runOffset = calculateRunOffset(handle);
            int runPages = calculateRunPages(handle);
            // 判断释放内存块 run 的前面有没有可用内存块
            long pastRun = getAvailRunByOffset(runOffset - 1);
            if (pastRun == -1) {
                return handle;
            }

            int pastOffset = calculateRunOffset(pastRun);
            int pastPages = calculateRunPages(pastRun);

            //is continuous
            if (pastRun != handle && pastOffset + pastPages == runOffset) {
                // 如果前一个可用内存块 run (pastOffset + pastPages == runOffset),
                // 说明前一个可用内存块和当前释放的内存块是连续的，就需要合并。

                // 删除前一个可用内存块，因为它要被合并
                removeAvailRun(pastRun);
                // 创建新的包含前一个可用内存块的新合并内存块
                handle = toRunHandle(pastOffset, pastPages + runPages, 0);
            } else {
                return handle;
            }
        }
    }

    long PoolChunk::collapseNext(long handle){
        for (;;) {
            int runOffset = calculateRunOffset(handle);
            int runPages = calculateRunPages(handle);
            // 从runsAvailMap中找到下一个内存块的handle。
            long nextRun = getAvailRunByOffset(runOffset + runPages);
            if (nextRun == -1) {
                return handle;
            }

            int nextOffset = calculateRunPages(nextRun);
            int nextPages = calculateRunPages(nextRun);

            //is continuous
            // 如果是连续的内存块，则移除下一个内存块handle，并将其page合并生成一个新的handle。
            if (nextRun != handle && runOffset + runPages == nextOffset) {
                //remove next run
                removeAvailRun(nextRun);
                handle = toRunHandle(runOffset, runPages + nextPages, 0);
            } else {
                return handle;
            }
        }
    }

    long PoolChunk::toRunHandle(int runOffset, int runPages, int inUsed){
        return static_cast<long> (runOffset) << RUN_OFFSET_SHIFT
                | static_cast<long> (runPages) << SIZE_SHIFT
                | static_cast<long> (inUsed) << IS_USED_SHIFT;
    }

    // allocate得到memoryMap数组的索引handle后，初始化PooledByteBuf
    void PoolChunk::initBuf(PooledByteBuf* buf, const char* buffer, long handle, int reqCapacity, PoolThreadCache* threadCache){
        // 根据索引获取到memoryMap数组中的元素，根据该元素提取出offset和size，用offset和size来初始化PooledByteBuf
        // 初始化池缓冲区PooledByteBuf(buf->init), 只需要计算出偏移量和大小就可以了。
        if (isRun(handle)) {
            // 如果是 Normal 规格类型
            // 真实偏移量就是 handle 的 (runOffset * pageSize)
            // 大小就是 handle 的 (runSize * pageSize)
            buf->init(this, buffer, handle, calculateRunOffset(handle) << m_pageShifts,
                    reqCapacity, calculateRunSize(m_pageShifts, handle), m_arena->m_parent->threadCache());
        } else {
            initBufWithSubpage(buf, buffer, handle, reqCapacity, threadCache);
        }
    }

    void PoolChunk::initBufWithSubpage(PooledByteBuf* buf, const char* buffer, long handle, int reqCapacity, PoolThreadCache* threadCache){
        int runOffset = calculateRunOffset(handle);
        int bitmapIdx = calculateBitmapIdx(handle);

        PoolSubpage* s = m_subpages[runOffset];
        assert(s->m_doNotDestroy);
        assert(reqCapacity <= s->m_elemSize);
        // 真实偏移量就是handle 的 ((runOffset * pageSize) + (bitmapIdx * s.elemSize))
        // 大小就是 elemSize
        int offset = (runOffset << m_pageShifts) + bitmapIdx * s->m_elemSize;
        buf->init(this, buffer, handle, offset, reqCapacity, s->m_elemSize, threadCache);
    }
}
#include "SizeClasses.h"

namespace buffer{
    SizeClasses::SizeClasses(int pageSize, int pageShifts, int chunkSize)
    :m_pageSize(pageSize),
    m_pageShifts(pageShifts),
    m_chunkSize(chunkSize),
    m_lookupMaxSize(0),
    m_nSubpages(0),
    m_nPSizes(0),
    m_smallMaxSizeIdx(0){
        int group = MathUtil::log2(chunkSize) + 1 - LOG2_QUANTUM;

        //generate size classes
        //[index, log2Group, log2Delta, nDelta, isMultiPageSize, isSubPage, log2DeltaLookup]
        m_sizeClasses.resize(group << LOG2_SIZE_CLASS_GROUP);
        m_nSizes = sizeClasses();

        //generate lookup table
        m_sizeIdx2sizeTab.reset(new int[m_nSizes]);
        m_pageIdx2sizeTab.reset(new int[m_nPSizes]);
        idx2SizeTab(m_sizeIdx2sizeTab.get(), m_pageIdx2sizeTab.get());

        m_size2idxTab.reset(new int[m_lookupMaxSize >> LOG2_QUANTUM]);
        size2idxTab(m_size2idxTab.get());
    }

    // 负责计算sizeClass表格
    int SizeClasses::sizeClasses(){
        int normalMaxSize = -1;

        int index = 0;
        int size = 0;

        int log2Group = LOG2_QUANTUM;
        int log2Delta = LOG2_QUANTUM;
        int ndeltaLimit = 1 << LOG2_SIZE_CLASS_GROUP;

        //First small group, nDelta start at 0.
        //first size class is 1 << LOG2_QUANTUM
        // 初始化第0组
        int nDelta = 0;
        while (nDelta < ndeltaLimit) {
            size = sizeClass(index++, log2Group, log2Delta, nDelta++);
        }
        log2Group += LOG2_SIZE_CLASS_GROUP;

        //All remaining groups, nDelta start at 1.
        // 初始化后面的size
        while (size < m_chunkSize) {
            nDelta = 1;

            while (nDelta <= ndeltaLimit && size < m_chunkSize) {
                size = sizeClass(index++, log2Group, log2Delta, nDelta++);
                normalMaxSize = size;
            }

            log2Group++;
            log2Delta++;
        }

        //chunkSize must be normalMaxSize
        assert(m_chunkSize == normalMaxSize);

        //return number of size index
        return index;
    }

    /**
     *
     * 计算sizeClass每一行内容
     * 
     * @param log2Group 内存块分组
     * @param log2Delta 增量大小的log2值
     * @param nDelta 增量乘数
     *
     * @return index in memoryMap
     */
    int SizeClasses::sizeClass(int index, int log2Group, int log2Delta, int nDelta){
        unsigned short isMultiPageSize;
        if (log2Delta >= m_pageShifts) {
            isMultiPageSize = yes;
        } else {
            int pageSize = 1 << m_pageShifts;
            int size = (1 << log2Group) + (1 << log2Delta) * nDelta;

            isMultiPageSize = size == size / pageSize * pageSize? yes : no;
        }

        int log2Ndelta = nDelta == 0? 0 : MathUtil::log2(nDelta);

        int remove = 1 << log2Ndelta < nDelta? yes : no;

        int log2Size = log2Delta + log2Ndelta == log2Group? log2Group + 1 : log2Group;
        if (log2Size == log2Group) {
            remove = yes;
        }

        unsigned short isSubpage = log2Size < m_pageShifts + LOG2_SIZE_CLASS_GROUP? yes : no;

        int log2DeltaLookup = log2Size < LOG2_MAX_LOOKUP_SIZE ||
                            log2Size == LOG2_MAX_LOOKUP_SIZE && remove == no
                ? log2Delta : no;

        unsigned short *sz = new unsigned short[7]{
                static_cast<unsigned short> (index), static_cast<unsigned short> (log2Group),
                static_cast<unsigned short> (log2Delta),
                static_cast<unsigned short> (nDelta), isMultiPageSize, isSubpage,
                static_cast<unsigned short> (log2DeltaLookup)
        };

        m_sizeClasses[index].reset(sz);
        // 内存块size的计算公式 
        int size = (1 << log2Group) + (nDelta << log2Delta);

        if (sz[PAGESIZE_IDX] == yes) {
            m_nPSizes++;
        }
        if (sz[SUBPAGE_IDX] == yes) {
            m_nSubpages++;
            m_smallMaxSizeIdx = index;
        }
        if (sz[LOG2_DELTA_LOOKUP_IDX] != no) {
            m_lookupMaxSize = size;
        }
        return size;
    }

    void SizeClasses::idx2SizeTab(int* sizeIdx2sizeTab, int* pageIdx2sizeTab){
        int pageIdx = 0;

        for (int i = 0; i < m_nSizes; i++) {
            unsigned short* sizeClass = m_sizeClasses[i].get();
            int log2Group = sizeClass[LOG2GROUP_IDX];
            int log2Delta = sizeClass[LOG2DELTA_IDX];
            int nDelta = sizeClass[NDELTA_IDX];

            int size = (1 << log2Group) + (nDelta << log2Delta);
            sizeIdx2sizeTab[i] = size;

            if (sizeClass[PAGESIZE_IDX] == yes) {
                pageIdx2sizeTab[pageIdx++] = size;
            }
        }
    }

    void SizeClasses::size2idxTab(int* size2idxTab){
        int idx = 0;
        int size = 0;

        for (int i = 0; size <= m_lookupMaxSize; i++) {
            int log2Delta = m_sizeClasses[i][LOG2DELTA_IDX];
            int times = 1 << log2Delta - LOG2_QUANTUM;

            while (size <= m_lookupMaxSize && times-- > 0) {
                size2idxTab[idx++] = i;
                size = idx + 1 << LOG2_QUANTUM;
            }
        }
    }

    /**
     * 根据给定的数组索引 [sizeIdx] 从 [sizeIdx2sizeTab] 数组中获取对应的内存大小
     *
     * @return size
     */
    int SizeClasses::sizeIdx2size(int sizeIdx){
        return m_sizeIdx2sizeTab[sizeIdx];
    }

    /**
     * 根据给定的数组索引 [sizeIdx]，计算出对应的内存大小
     *
     * @return size
     */
    int SizeClasses::sizeIdx2sizeCompute(int sizeIdx){
        /**
         * LOG2_SIZE_CLASS_GROUP = 2: 说明每一组group的数量就是4个；
         * LOG2_QUANTUM = 4: 最小容量从16，即2的4次方开始。
         * 计算 size 的值，必须要区分第一组和后面的组，这个计算逻辑不同。
         */
        // 因为每一组数量就是4， >> LOG2_SIZE_CLASS_GROUP 就是得到组group；
        // 0 表示第一组，1就是第二组
        int group = sizeIdx >> LOG2_SIZE_CLASS_GROUP;
        // (1 << LOG2_SIZE_CLASS_GROUP) - 1 = 3;
        // 和 sizeIdx 进行位与(&) 运算就是得到整除4的余数
        int mod = sizeIdx & (1 << LOG2_SIZE_CLASS_GROUP) - 1;
        /**
         * 如果是第一组(group == 0)，那么 groupSize就是0，最后size只通过 modSize 得到；size=groupSize(0)+modSize
         * 其他组，那么组的基础size： 1 << ( 5 + group), LOG2_QUANTUM + LOG2_SIZE_CLASS_GROUP - 1 的值就是 5，
         * 这个计算公式就相当于 1 << log2Group; log2Group = 5 + group
         */
        int groupSize = group == 0? 0 :
                1 << LOG2_QUANTUM + LOG2_SIZE_CLASS_GROUP - 1 << group;
        // 第一组 shift == 1
        // 其他组 shift = group，其实第二组也是 shift == group == 1
        int shift = group == 0? 1 : group;
        // lgDelta 就是 log2Delta 的值，第一组和第二组都是4，第三组是5
        int lgDelta = shift + LOG2_QUANTUM - 1;
        /**
         * 除第一组外，其他组的 nDelta 都是从1开始的，所以 nDelta = mod + 1；
         * 第一组的 nDelta 从0开始的，但是这里也使用了 mod + 1，那是因为我们直接将 groupSize = 0
         */
        int modSize = mod + 1 << lgDelta;
        return groupSize + modSize;
    }

    /**
     * 根据给定的数组索引 [pageIdx] 从 [pageIdx2sizeTab] 数组中获取对应的内存大小
     *
     * @return size(为pagesize的整数倍)
     */ 
    long SizeClasses::pageIdx2size(int pageIdx) {
        // 根据给定的数组索引 pageIdx 从 [pageIdx2sizeTab] 数组中获取对应的内存大小
        return m_pageIdx2sizeTab[pageIdx];
    }

    long SizeClasses::pageIdx2sizeCompute(int pageIdx) {
        // 根据给定的数组索引 pageIdx，计算出对应的内存大小
        /**
         * 这个的逻辑和 sizeIdx2sizeCompute() 几乎一模一样，
         * 唯一不同的是 sizeIdx2sizeCompute() 是从 LOG2_QUANTUM (4) 开始
         * 而这个方法是从 pageShifts (pageSize 13) 开始
         */
        int group = pageIdx >> LOG2_SIZE_CLASS_GROUP;
        int mod = pageIdx & (1 << LOG2_SIZE_CLASS_GROUP) - 1;

        long groupSize = group == 0? 0 :
                1L << m_pageShifts + LOG2_SIZE_CLASS_GROUP - 1 << group;

        int shift = group == 0? 1 : group;
        int log2Delta = shift + m_pageShifts - 1;
        int modSize = mod + 1 << log2Delta;

        return groupSize + modSize;
    }

    // @return sizeIdx(sizeIdx2sizeTab 数组的下标)
    int SizeClasses::size2SizeIdx(int size){
        // 根据请求大小 size 得到规格化内存的索引值[sizeIdx]
        if (size == 0) {
            return 0;
        }
        if (size > m_chunkSize) {
            // 如果大于 chunkSize，说明它是一个 Huge 类型内存块；
            // 那么返回 nSizes，超出 sizeIdx2sizeTab 数组的范围。
            return m_nSizes;
        }
        // 是否需要进行内存对齐，就是 size 必须是 directMemoryCacheAlignment 的倍数。
        // directMemoryCacheAlignment 必须是 2 的幂数，这样才能进行位运算。
        // if (m_directMemoryCacheAlignment > 0) {
        //     size = alignSize(size);
        // }

        // 如果 size 小于等于 lookupMaxSize，
        // 那么就在 size2idxTab 数组范围内，
        // 可以通过 size2idxTab 数组快速得到 sizeIdx 值。
        // SizeClasses将一部分较小的size与对应index记录在size2idxTab作为位图
        // 直接查询size2idxTab，避免重复计算
        if (size <= m_lookupMaxSize) {
            // size-1 / MIN_TINY
            // >> LOG2_QUANTUM 相当于 (size - 1) / 16，因为肯定都是 16 的倍数
            // 获取 sizeIdx 再通过 sizeIdx2sizeTab 数组得到对应大小
            return m_size2idxTab[size - 1 >> LOG2_QUANTUM];
        }
        /**
         * ((size << 1) - 1) 是向上得到 size 对应的 log2 的数；
         * 例如 8 得到 3，9 得到 4，15 得到 4, 16 得到 4， 17 得到 5.
         *
         *  LOG2_SIZE_CLASS_GROUP + LOG2_QUANTUM + 1 = 7。
         *  第一组规格内存块 size 最大为 64， x = 6
         *  如果需要大小为 65，计算后的值就是 7， 在第二组的第一个规格内存块。
         *
         *  只要 x < 7，这个 size 就是在第一组内；
         *  x >= 7，这个 size 在除第一组外的其他组内。
         *
         *  第一组和其他组的算法不一样
         */
        int x = MathUtil::log2((size << 1) - 1);
        // 一、定位组
        // 判断是第一组还是其他组。 sizeClasses表格中 0-3第0组 4-7第1组
        // 第一组 shift 是 0，第二组是 1， 第三组是 2，依次递增1
        int shift = x < LOG2_SIZE_CLASS_GROUP + LOG2_QUANTUM + 1
                ? 0 : x - (LOG2_SIZE_CLASS_GROUP + LOG2_QUANTUM);
        // 因为每一组默认是 4 个，就是 shift << LOG2_SIZE_CLASS_GROUP，shift<<2
        // 得到这一组的开始值。
        int group = shift << LOG2_SIZE_CLASS_GROUP;
        // 判断是第一组还是其他组。
        // 第一组就是 4， 第二组也是 4，第三组是5，依次递增1
        int log2Delta = x < LOG2_SIZE_CLASS_GROUP + LOG2_QUANTUM + 1
                ? LOG2_QUANTUM : x - LOG2_SIZE_CLASS_GROUP - 1;
        // 二、定位组中的位 因为每个组有4个位，需继续精确定位
        int deltaInverseMask = -1 << log2Delta;
        // & deltaInverseMask 将申请内存大小最后log2Delta个bit位设置为0，可以理解为减去n
        // >> log2Delta，右移log2Delta个bit位==除以(1 << log2Delta)
        //               结果就是(nDelta - 1 + 2 ^ LOG2_SIZE_CLASS_GROUP)
        // & (1 << LOG2_SIZE_CLASS_GROUP) - 1， 取最后的LOG2_SIZE_CLASS_GROUP(2)个bit位的值，结果就是mod
        // size-1 为了申请内存等于内存块size时避免分配到下一个内存块size中，即n == (1 << log2Delta)
        int mod = (size - 1 & deltaInverseMask) >> log2Delta &
                    (1 << LOG2_SIZE_CLASS_GROUP) - 1;

        // size = (nDelta - 1 + 2^LOG2_SIZE_CLASS_GROUP) * (1 << log2Delta) + n
        // nDelta-1=mod 2^LOG2_SIZE_CLASS_GROUP=group 0 < n <= (1 << log2Delta)
        // 找到满足条件的第一个区间！
        return group + mod;
    }

    int SizeClasses::pages2pageIdx(int pages){
        // 根据请求大小 [pages] 得到规格化内存的索引值[pageIdx]
        return pages2pageIdxCompute(pages, false);
    }

    int SizeClasses::pages2pageIdxFloor(int pages){
        // 根据请求大小 [pages] 得到规格化内存的索引值[pageIdx]
        // 与 pages2pageIdx(int pages) 方法不同，它会向下求得最近的[pageIdx]
        return pages2pageIdxCompute(pages, true);
    }

    int SizeClasses::pages2pageIdxCompute(int pages, bool floor){
        int pageSize = pages << m_pageShifts;
        if (pageSize > m_chunkSize) {
            return m_nPSizes;
        }

        int x = MathUtil::log2((pageSize << 1) - 1);

        int shift = x < LOG2_SIZE_CLASS_GROUP + m_pageShifts
                ? 0 : x - (LOG2_SIZE_CLASS_GROUP + m_pageShifts);

        int group = shift << LOG2_SIZE_CLASS_GROUP;

        int log2Delta = x < LOG2_SIZE_CLASS_GROUP + m_pageShifts + 1?
                m_pageShifts : x - LOG2_SIZE_CLASS_GROUP - 1;

        int deltaInverseMask = -1 << log2Delta;
        int mod = (pageSize - 1 & deltaInverseMask) >> log2Delta &
                    (1 << LOG2_SIZE_CLASS_GROUP) - 1;

        int pageIdx = group + mod;
        // floor 是true， 当 pageIdx 对应的内存大小比 pages 大时，pageIdx要减一
        if (floor && m_pageIdx2sizeTab[pageIdx] > pages << m_pageShifts) {
            pageIdx--;
        }

        return pageIdx;
    }

    // int SizeClasses::alignSize(int size)
    // {
    //     int delta = size & m_directMemoryCacheAlignment - 1;
    //     return delta == 0? size : size + m_directMemoryCacheAlignment - delta;
    // }

    int SizeClasses::normalizeSize(int size){
        // 以指定的大小和对齐方式分配对象，使可用大小标准化。
        if (size == 0) {
            return m_sizeIdx2sizeTab[0];
        }
        // if (m_directMemoryCacheAlignment > 0) {
        //     size = alignSize(size);
        // }
        if (size <= m_lookupMaxSize) {
            int ret = m_sizeIdx2sizeTab[m_size2idxTab[size - 1 >> LOG2_QUANTUM]];
            assert(ret == normalizeSizeCompute(size));
            return ret;
        }
        return normalizeSizeCompute(size);
    }

    int SizeClasses::normalizeSizeCompute(int size){
        int x = MathUtil::log2((size << 1) - 1);
        int log2Delta = x < LOG2_SIZE_CLASS_GROUP + LOG2_QUANTUM + 1
                ? LOG2_QUANTUM : x - LOG2_SIZE_CLASS_GROUP - 1;
        int delta = 1 << log2Delta;
        int delta_mask = delta - 1;
        return size + delta_mask & ~delta_mask;
    }
}
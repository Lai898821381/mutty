<h2 align='center'>jemalloc4内存池</h2>

#### 组件

##### PoolArena

* 负责内存的分配和回收，是申请内存线程分配内存的主要组件。
* 包含smallSubpagePools数组和不同内存使用率的PoolChunkList，分别管理两种不同规格的内存块。smallSubpagePools管理小于28KB的内存分配区域，PoolChunkList中存放不同使用率的PoolChunk。
* 申请内存小于28KB时，按照page分配，超过时，按照run分配，一个run包含多个page。
* 小于40960B的内存释放后，由thread_local类型的PoolThreadCache对象缓存。PoolThreadCache对象包含一个Entry队列，Entry存放chunk，handle，buffer等信息，指示PoolChunk中的一块区域。大内存释放根据PoolChunk的内存使用率，存入对应内存使用率区间的PoolChunkList中，在下次分配时根据不同内存使用率的PoolChunkList查找对应PoolChunk进行分配。会先在PoolThreadCache对象中查找缓存，查找不到时，才会在PoolArena中分配内存。

##### PoolThreadCache

* 包含small和normal缓存数组，small和normal缓存由MemoryRegionCache的派生类管理，区别在于初始化buffer的方法不同。
* 当发生缓存时，从Entry recycler取得或生成Entry结构体，其中保存了PoolChunk的内存信息，放入队列中。
* 分配内存时，从队列中弹出元素，并将Entry放回recycler，用于下次缓存其他内存，Entry recycler可重复利用Entry，减少下次缓存内存时的new Entry开销。

##### PoolChunk

* PoolChunk大小为16MB，包含有优先级队列LongPriorityQueue数组m_runsAvail。
* runsAvail用于管理所有空闲的run内存块。run根据拥有页数的不同分为40种，因此runAvail的长度为40，每一个LongPriorityQueue管理一种类型的run，队列的优先级是run的页偏移量runOffset。初始时，runsAvail在下标39的优先级队列中存放整块PoolChunk的run内存块，即runOffset为0,size为chunkSize/pageSize。然后不断分裂，下标逐渐减小。

* 申请的内存小于28KB，按照subpage分配：申请一个Normal内存块，然后把Normal内存交给一个PoolSubpage对象进行维护，这个PoolSubpage被插入PoolArena的m_smallSubpagePools数组中进行管理。
* 申请的内存大于28KB，根据分配内存大小获取对应的LongPriorityQueue，然后弹出offset最小的元素，根据需要的pages数目切分run，将多余的pages放回runsAvail和runAvailMap，用于下次分配。
* 释放内存时，先释放PoolSubpage，后释放run。释放run时，会查找该run前后是否有可合并空闲run，会合并成更大的run再释放。

##### PoolSubpage

* 为PoolChunk中的一个Normal内存块，采用位图bitmap管理内存块，用long类型的位bit记录page的所有可分配内存单元，一个PoolSubpage对象中的内存块尺寸相同，新建的PoolSubpage都会加入PoolArena的smallpagePools链表中。
* PoolChunk为subpage分配内存时，会根据申请的内存大小找到对应的分配大小，将page分成(int)(pageSize/elemSize)份，空闲内存块为bitmap数组的该元素二进制为0。subpage分配完全就会从subpage池中移除。
* PoolSubpage释放时，会把它释放区域的bitmap对应m_bitmap对应元素对应位置置为0，如果该PoolSubpage不在PoolSubpage池，就将其加入，内存释放完后，如果不是smallpagePools链表的最后一个Poolsubpage，则会被移出。

##### PoolByteBufAllocator

* 维护arena的PoolArena数组，该数组初始化长度为cpu虚拟核数目
* 提供了静态函数ALLOCATOR，工作于单例模式，主要用于分配内存池缓冲PooledByteBuf。每个线程都分配了一个thread_local的PoolThreadCache，通过负载均衡将线程绑定到分配线程数目最少的PoolArena。

##### PooledByteBuf

* 包含writerIndex和readerIndex，写时后移writerIndex，读时后移readerIndex

  ```c++
  /// +-------------------+------------------+------------------+
  /// | discardable bytes |  readable bytes  |  writable bytes  |
  /// |                   |     (CONTENT)    |                  |
  /// +-------------------+------------------+------------------+
  /// |                   |                  |                  |
  /// 0      <=      readerIndex   <=   writerIndex    <=   capacity
  ```


##### Buffer

* 主要的对外工作类，用于申请内存池缓冲，并提供了相关接口进行Buffer的读写。

#### 实验结果

* 单线程进行分配和释放4000000次，总共用时2.64974秒；4线程分配释放4000000次，总共用时0.949077。多线程分配和释放性能显著高于单线程，证明在多线程下效果更好。

  | 线程数 | 总分配释放数  | 用时      |
  | ------ | ------------ | -------  |
  | 1      | 4000000      | 2.64974  |
  | 4      | 4000000      | 0.949077 |
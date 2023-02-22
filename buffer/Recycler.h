#ifndef BUFFER_RECYCLER_H
#define BUFFER_RECYCLER_H

#include <assert.h>
#include <stack>

namespace buffer{
    // 对象池的实现难点在于线程安全。
    // Recycler中将主线程和非主线程回收对象划分到不同的存储空间中（stack#elements和WeakOrderQueue.Link#elements）
    // 并且对于WeakOrderQueue.Link#elements，存取操作划分到两端进行（非主线程从尾端存入，主线程从首部开始读取），
    // 从而减少同步操作，并保证线程安全。

    template<typename T>
    class Recycler{
        typedef T value_type;

        // static const int INITIAL_CAPACITY = 256;

        static thread_local std::stack<void *> tStack; 

        public:
            Recycler(){}
            Recycler(const Recycler & ) = delete;
            virtual ~Recycler() {};
            virtual value_type * newObject() = 0;
            // Recycler#threadLocal中存放了每个线程对应的Stack。
            // Recycler#get中首先获取属于当前线程的Stack，再从该Stack中获取对象，也就是，每个线程只能从自己的Stack中获取对象。
            inline value_type * get(){
                if(tStack.empty()){
                    return newObject();
                }
                value_type* objectItem = static_cast<value_type *> (tStack.top());
                tStack.pop();
                return objectItem;
            }
            inline bool recycle(value_type* object) {
                assert(object != nullptr);
                // assert(tStack.size() <= INITIAL_CAPACITY);
                tStack.push(object);
                return true;
            }
    };
    template<typename T>
    thread_local std::stack<void *> Recycler<T>::tStack = std::stack<void *>();

    /*
    class WeakOrderQueue{
        // Head#link指向Link链表首对象
        Head head;  
        // 指向Link链表尾对象
        Link tail;
        // 指向WeakOrderQueue链表下一对象
        WeakOrderQueue next;
        // 所属线程
        WeakReference<Thread> owner;
      public:
        void add(DefaultHandle<?> handle) {
            handle.lastRecycledId = id;

            // 控制回收频率，避免WeakOrderQueue增长过快。
            // 每8个对象都会抛弃7个，回收一个
            if (handleRecycleCount < interval) {
                handleRecycleCount++;
                return;
            }
            handleRecycleCount = 0;


            Link tail = this.tail;
            int writeIndex;
            // 当前Link#elements已全部使用，创建一个新的Link
            if ((writeIndex = tail.get()) == LINK_CAPACITY) {
                Link link = head.newLink();
                if (link == null) {
                    return;
                }
                this.tail = tail = tail.next = link;
                writeIndex = tail.get();
            }
            // 存入缓存对象
            tail.elements[writeIndex] = handle;
            handle.stack = null;
            // 延迟设置Link#elements的最新索引（Link继承了AtomicInteger）
            // 这样在该stack主线程通过该索引获取elements缓存对象时，保证elements中元素已经可见。
            tail.lazySet(writeIndex + 1);
            // 把WeakOrderQueue中的对象迁移到Stack中。
            // stack.pop()调用
            bool transfer(Stack<?> dst) {
                Link head = this.head.link;
                if (head == null) {
                    return false;
                }
                // head.readIndex 标志现在已迁移对象下标
                // head.readIndex == LINK_CAPACITY，表示当前Link已全部移动，查找下一个Link
                if (head.readIndex == LINK_CAPACITY) {
                    if (head.next == null) {
                        return false;
                    }
                    head = head.next;
                    this.head.relink(head);
                }
                // 计算待迁移对象数量
                // !Link继承了AtomicInteger
                final int srcStart = head.readIndex;
                int srcEnd = head.get();
                final int srcSize = srcEnd - srcStart;
                if (srcSize == 0) {
                    return false;
                }
                //  计算Stack#elements数组长度，不够则扩容
                final int dstSize = dst.size;
                final int expectedCapacity = dstSize + srcSize;

                if (expectedCapacity > dst.elements.length) {
                    final int actualCapacity = dst.increaseCapacity(expectedCapacity);
                    srcEnd = min(srcStart + actualCapacity - dstSize, srcEnd);
                }

                if (srcStart != srcEnd) {
                    final DefaultHandle[] srcElems = head.elements;
                    final DefaultHandle[] dstElems = dst.elements;
                    int newDstSize = dstSize;
                    //  计算Stack#elements数组长度，不够则扩容
                    for (int i = srcStart; i < srcEnd; i++) {
                        DefaultHandle<?> element = srcElems[i];
                        ...
                        srcElems[i] = null;
                        // 控制回收频率
                        if (dst.dropHandle(element)) {
                            continue;
                        }
                        element.stack = dst;
                        dstElems[newDstSize ++] = element;
                    }
                    // 当前Link对象已全部移动，修改WeakOrderQueue#head的link属性，指向下一Link，这样前面的Link就可以被垃圾回收了。
                    if (srcEnd == LINK_CAPACITY && head.next != null) {
                        this.head.relink(head.next);
                    }

                    head.readIndex = srcEnd;
                    // dst.size == newDstSize 表示并没有对象移动，返回false
                    // 否则更新dst.size
                    if (dst.size == newDstSize) {
                        return false;
                    }
                    dst.size = newDstSize;
                    return true;
                } else {
                    // The destination stack is full already.
                    return false;
                }
            }
    };


    template<typename T>
    class Stack{
        typedef T value_type;
        Recycler<T>* m_recycler;
        shared_ptr<Thread> threadRef;
        // 主线程回收的对象
        vector<value_type> elements;
        // elements最大长度
        int maxCapacity;
        // elements索引
        int size;
        // 非主线程回收的对象
        volatile WeakOrderQueue head;   
        void push(value_type* object) {
            Thread currentThread = Thread.currentThread();
            if (threadRef.get() == currentThread) {
                pushNow(object); // 当前线程是主线程，直接将对象加入到Stack#elements中
            } else {
                pushLater(object, currentThread); // 当前线程非主线程，需要将对象放到对应的WeakOrderQueue中
            }
        }
        void pushLater(value_type* item, Thread thread) {
            ...
            // DELAYED_RECYCLED是一个FastThreadLocal，可以理解为Netty中的ThreadLocal优化类。
            // 它为每个线程维护了一个Map，存储每个Stack和对应WeakOrderQueue。
            // 这里获取的delayedRecycled变量是仅用于当前线程的。
            // delayedRecycled.get获取的WeakOrderQueue，是以Thread + Stack作为维度区分的，只能是一个线程操作。
            Map<Stack<?>, WeakOrderQueue> delayedRecycled = DELAYED_RECYCLED.get();
            WeakOrderQueue queue = delayedRecycled.get(this);
            if (queue == null) {
                // 当前WeakOrderQueue数量超出限制，添加WeakOrderQueue.DUMMY作为标记
                if (delayedRecycled.size() >= maxDelayedQueues) {
                    delayedRecycled.put(this, WeakOrderQueue.DUMMY);
                    return;
                }
                // 构造一个WeakOrderQueue，加入到Stack#head指向的WeakOrderQueue链表中，并放入DELAYED_RECYCLED。这时是需要一下同步操作的。
                if ((queue = newWeakOrderQueue(thread)) == null) {
                    return;
                }
                delayedRecycled.put(this, queue);
            } else if (queue == WeakOrderQueue.DUMMY) {
                // 遇到WeakOrderQueue.DUMMY标记对象，直接抛弃对象
                return;
            }
            // 将缓存对象添加到WeakOrderQueue中。
            queue.add(item);
        }
        value_type* pop() {
            int size = this.size;
            if (size == 0) {
                // elements没有可用对象时，将WeakOrderQueue中的对象迁移到elements
                if (!scavenge()) {
                    return null;
                }
                size = this.size;
                if (size <= 0) {
                    return null;
                }
            }
            // 从elements中取出一个缓存对象
            size --;
            value_type* ret = elements[size];
            elements[size] = null;
            this.size = size;

            ...
            return ret;
        }
    };
    */

}
#endif // BUFFER_RECYCLER_H
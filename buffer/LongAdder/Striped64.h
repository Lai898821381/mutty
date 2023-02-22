#ifndef Striped64_H
#define Striped64_H

#include <atomic>
#include <random>
#include <unistd.h>

// 管理cells和映射线程的功能
// JDK源码术语dynamic striping指把一个变量拆成多个，放在一个可扩展的数组中管理
// Striped64指的是数组中的变量是64位。
namespace buffer{
    // 一个简化的仅支持原始访问和CAS 的 AtomicLong
    // 使用注解避免cache伪共享
    template<class T>
    struct Cell{
        long p0,p1,p2,p3,p4,p5,p6;
        volatile std::atomic<T> value;
        long p7,p8,p9,p10,p11,p12,p13;
        Cell() { value.store(0); }
        explicit Cell(T x) { value.store(x); }
        ~Cell(){};
        Cell(const Cell&) = delete;
        Cell& operator = (const Cell&) = delete;
    };

    extern thread_local int probe;

    template<class T>
    class Striped64{
    public:
        int NCPU = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));

        // 基数，在两种情况下会使用:
        // 1、没有遇到并发竞争时,直接使用base累加数值:
        // 2，初始化cells数组时，必须要保证cells数组只被初始化一次（即只有一个线程能对cells初始化)
        // 其它竞争失败的线程会将数值累加到base 上
        std::atomic<T> m_base;
        // 自旋标识，在对cells进行初始化、扩容、在nullptr位置创建新的Cell对象时，需要通过CAS操作把此
        // 标识设置为1（busy，忙标识，相当于加锁），取消busy时可以直接使用cellsBusy = 0，相当于释放锁
        std::atomic<bool> m_cellsBusy = ATOMIC_VAR_INIT(false);
        Cell<T> ** m_cells;
        int nCells;

        Striped64()
        :m_cells(nullptr),
        m_base(0),
        nCells(0)
        {}

        ~Striped64() {
            if(nullptr != m_cells) {
                for(int i = 0; i < nCells; i++){
                    m_cells[i] = nullptr;
                }
                delete[] m_cells;
                m_cells = nullptr;
            }
        }

        inline bool casBase(T desired, T& expected) {
            return m_base.compare_exchange_weak(expected, desired);
        }

        inline bool casCellValue(T desire, T value, volatile std::atomic<T>& expected) {
            return expected.compare_exchange_weak(value, desire);
        }
        // 使用CAS将cells自旋标识更新为1
        // 更新为0时可以不用CAS，直接使用cellsBusy就行
        inline bool casCellsBusy() {
            bool exp = false;
            return m_cellsBusy.compare_exchange_strong(exp, true);
        }

        void localinit(){
            std::random_device rd;
            std::mt19937 eng(rd());
            std::uniform_int_distribution<int> distr(1, 2147483647);
            probe = distr(eng);
        }
        // 重置当前线程的哈希值
        static int advanceProbe(int probe) {
            probe ^= probe << 13;   // xorshift
            probe ^= probe >> 17;
            probe ^= probe << 5;
            return probe;
        }
        // wasUncontended只有cells初始化之后，并且当前线程竞争修改失败才会是false
        void longAccumulate(T x, bool wasUncontended, int index) {
            if (index == 0) {
                // 给当前线程生成一个非0的hash值
                localinit(); // force initialization
                index = probe;
                wasUncontended = true;
            }
            // 扩容意向，false一定不会扩容，true可能会扩容
            for (bool collide = false;;) {       // True if last slot nonempty
                Cell<T>** cs; Cell<T>* c; int n; T v;
                // Cell[]数组已经初始化，其他线程也进入了LongAccumulate方法
                // 当前线程应该将数据写入到对应的cell中
                if ((cs = m_cells) != nullptr && (n = nCells) > 0) {
                    // 当前线程的hash值运算后映射得到的Cell单元为nullptr，说明该Cell没有被使用
                    // 每个线程通过对cells[threadLocalRandomProbe%cells.length] 位置上的cell元素中的value值做累加
                    // 相当于将线程绑定到数组cells中的某个cell元素对象上
                    // 当前线程对应下标位置为nullptr，需要创建新cell
                    if ((c = cs[(n - 1) & index]) == nullptr) {
                        // Cell[]数组没有正在扩容
                        if (!m_cellsBusy) {       // Try to attach new Cell
                            // 创建一个Cell单元
                            Cell<T>* r = new Cell<T>(x);   // Optimistically create
                            // 尝试加锁，成功后cellsBusy=1
                            if (!m_cellsBusy && casCellsBusy()) {
                                bool created = false;
                                    // j=(m-1)&index为哈希映射index=hash&mask
                                    // mask=length-1,probe为hashcode
                                    // rs表示当前cells引用，j为当前线程命中的下标，m为cells长度
                                    Cell<T> ** rs; int m, j;
                                    // 有锁的情况下，再检测一遍之前的判断
                                    // 考虑别的线程可能执行了扩容，这里重新赋值重新判断
                                    // 条件1 2 恒成立，条件3 rs[j = (m - 1) & index] == nullptr
                                    // 为了防止时间片被强占导致 cell被其他线程初始化过该位置，而再次初始化
                                    if ((rs = m_cells) != nullptr && (m = nCells) > 0 &&
                                        rs[j = (m - 1) & index] == nullptr) {
                                        // 将Cell单元附到Cell[]数组上
                                        rs[j] = r;
                                        created = true;
                                    }
                                m_cellsBusy = false; // 清空自旋标识，释放锁
                                // 如果原本为null的Cell单元是由自己进行第一次累积操作，那么任务已经完成了，所以可以退出循环
                                if(created) break; 
                                continue;           // Slot is now non-empty 不是自己进行第一次累积操作，重头再来
                            }
                        }
                        // 执行这一句是因为cells被加锁了
                        // 尚未new cell时，扩容是不合理的
                        // 不能往下继续执行第一次的赋值操作（第一次累积），所以还不能考虑扩容
                        collide = false;
                    }
                    // case1.2
                    // wasUncontended表示前一次更新Cell单元是否成功
                    // 前面一次CAS更新a.value（进行一次累积）的尝试已经失败了，说明已经发生了线程竞争
                    // 会再次进入case1，这样如果再次冲突就再次走到这里，会进入下一个case1.3
                    else if (!wasUncontended)       // CAS already known to fail
                        // 情况失败标识，后面去重新算一遍线程的hash值
                        wasUncontended = true;      // Continue after rehash
                    // 尝试CAS更新a.value（进行一次累积） ------ 标记为分支A
                    // case1.3 当前线程rehash过，然后新命中的cell不为空
                    // 写成功就退出循环
                    // 失败，说明rehash新命中的新cell也有竞争，重试一次
                    else if (casCellValue(v + x, v = c->value.load(), c->value))
                        // 成功了就完成了累积任务，退出循环
                        break;
                    // cell数组已经是最大的了，或者中途发生了扩容操作。因为NCPU不一定是2^n，所以这里用 >=
                    // cells!=cs说明其他线程已经扩容过了，当前线程rehash后重试即可
                    else if (n >= NCPU || m_cells != cs)
                        collide = false;            // At max size or stale
                    // case1.5
                    // 扩容意向设置为true，但是不一定真的发生扩容
                    // 这行代码会导致再次重试一下，collide之前总是false
                    // 这次设置为true之后，以后就不会再重试了
                    else if (!collide)
                        collide = true;
                    // case1.6 真正扩容
                    else if (!m_cellsBusy && casCellsBusy()) {
                            if (m_cells == cs){        // Expand table unless stale
                                // 扩容一倍
                                m_cells = new Cell<T>*[n << 1];
                                for(int i = 0; i < n; ++i){
                                    m_cells[i] = cs[i];
                                }
                                //  Arrays.copyOf(cs, n << 1);
                                nCells = n << 1;
                            }
                            m_cellsBusy = false;
                        collide = false;
                        continue;                   // Retry with expanded table
                    }
                    // 重置当前线程的hash值
                    index = advanceProbe(index);
                }
                // Cell[]数组未初始化
                // casCellsBusy() 将cellsBusy置为1-加锁状态
                // 条件二m_cells==cs，其他线程在本线程给cs赋值之前，修改了cells
                else if (!m_cellsBusy && m_cells == cs && casCellsBusy()) {
                    bool init = false;
                        // 然后，初始化Cell[]数组（初始大小为2），根据当前线程的hash值计算映射的索引
                        // 并创建对应的Cell对象，Cell单元中的初始值x就是本次要累加的值。
                        if (m_cells == cs) {
                            // CAS避免不了ABA问题，这里再检测一次，如果还是null，或者空数组，那么就执行初始化
                            Cell<T> ** rs = new Cell<T>*[2];
                            nCells = 2;
                            rs[index & 1] = new Cell<T>(x); // 对其中一个单元进行累积操作，另一个不管，继续为null
                            m_cells = rs;
                            rs = nullptr;
                            init = true;
                        }
                        m_cellsBusy = false;
                    if(init) break;
                }
                // Fall back on using base
                // case1:Cell[]数组正在初始化中m_cellsBusy
                // case2:cells被其他线程初始化后m_cells!=cs
                // 此时另一个线程也进入LongAccumulate就会进入这个分支
                // 直接在基数base上进行累加操作
                else if (casBase(v + x, v = m_base.load()))
                    break;
            }
        }

    };
}

#endif //Striped64_H
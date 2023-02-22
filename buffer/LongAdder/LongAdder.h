#ifndef LONGADDER_H
#define LONGADDER_H

#include "Striped64.h"
#include <iostream>

namespace buffer{
    class LongAdder : public Striped64<long>{
    public:
        LongAdder(){};
        ~LongAdder(){};
        // 只有从未出现过并发冲突的时候，base基数才会使用到
        // 一旦出现了并发冲突，之后所有的操作都只针对 Cell[]数组中的单元Cell。
        // 如果 Cell[]数组未初始化，会调用父类的 longAccumelate去初始化 Cell[]
        // 如果 Cell[]已经初始化但是冲突发生在 Cell单元内
        // 则也调用父类的 longAccumelate，此时可能就需要对 Cell[]扩容了。
        // ！！尽量减少热点冲突，不到最后万不得已，尽量将CAS操作延迟
        void add(long x) {
            // cs是cells引用，b获取的base值，v期望值，
            // m为cells数组的长度，c表示当前命中的cell单元格
            Cell<long> ** cs; long b, v; int m; Cell<long>* c;
            // 初始时cs=cells=nullptr，Thread A会调用caseBase
            // 没有并发时，会成功将base变为base+x
            // 如果线程A B C D线性执行, casBase永远不会失败，所有值都会累计到base中
            // case1已经初始化过，当前线程应该将数据写入对应cell中
            // case2未初始化，当前所有线程应该将数据写入base中
            if ((cs = m_cells) != nullptr || !casBase(b + x, b = m_base.load())) {
                int index = probe;
                // 为true表示未发生竞争，false表示发生竞争
                bool uncontended = true;
                // 有线程冲突，导致casBase失败，会进入if
                // 再次判断Cell[]槽数组有没有初始化过(没有初始化过说明未出现过并发冲突)
                // 如果初始化过了，以后所有的CAS操作都只针对槽中的Cell
                // 否则，进入longAccumulate方法。
                // cells未初始化
                // !!!!!多线程写同一个cell发生竞争
                // 当前线程对应cell为空、cas失败时
                // 需要扩容的时候就进入longAccumulate
                if (cs == nullptr || (m = nCells - 1) < 0 ||  // 成立 说明cells未初始化，是通过casBase进入的 false表示初始化了，线程找对应cell写值
                    (c = m_cells[index & m]) == nullptr || // m=2^i-1，为true说明当前线程的下标为空，需要创建
                    !(uncontended = casCellValue(v + x, v = c->value.load(), c->value)))
                    longAccumulate(x, uncontended, index);
            }
        }
        // 返回累加的和，也就是“当前时刻”的计数值
        // 此返回值可能不是绝对准确的，因为调用这个方法时还有其他线程可能正在进行计数累加，
        // 方法的返回时刻和调用时刻不是同一个点，在有并发的情况下，这个值只是近似准确的计数值
        // 高并发时，除非全局加锁，否则得不到程序运行中某个时刻绝对准确的值，但是全局加锁在高并发情况下是下下策
        // 在很多的并发场景中，计数操作并不是核心，这种情况下允许计数器的值出现一点偏差，此时可以使用LongAdder
        // 在必须依赖准确计数值的场景中，应该自己处理而不是使用通用的类
        long sum() {
            Cell<long> ** cs = m_cells;
            Cell<long> *c;
            long sum = m_base.load();
            std::cout<<"m_base: "<<sum<<std::endl;
            if (cs != nullptr) {
                std::cout<<"c->value.load:";
                for(int i = 0; i < nCells; ++i){
                    if((c = cs[i]) != nullptr){
                        std::cout<<c->value.load()<<" ";
                        sum += c->value.load();
                    }
                }
            }
            std::cout<<std::endl;
            return sum;
        }
};
}

#endif //LONGADDER_H
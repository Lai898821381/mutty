<h2 align='center'>高性能C++原子计数类</h2>

### 功能

* 在高并发条件下实现高性能统计

* 实现了Cell类型的结构体，进行字节填充避免触发cacheline伪共享机制。

* 定义了Striped64类，提供高性能的原子变量操作支持，包含base和cells两个原子成员变量，未发生争用时，用base进行计数，cells为Cell数组，线程间发生争用时，为每个线程分配一个槽，线程后续将在对应的槽中计数，极大避免了多线程cas锁争用。
* 基于Striped64构造的LongAdder的long类型计数类，具备原子性，在CPU数量较多且并发较高的情况下**性能明显高于atomic<long>的原子变量**。LongAdder读操作并不是原子操作，无法得到某个时刻的精确值。例如，在调用sum()的时候，如果还有线程正在写入，那么sum()返回的就不是当时的精确值。适用于统计和显示系统中的某些高速变化的状态。
* 基于Striped64构造的DoubleAdder的double类型计数类，提供了**c++不支持的浮点数原子算术操作**

### 结果

* 实验测试函数

  ```c++
  LongAdder LAnums;
  DoubleAdder DAnums;
  atomic<long> ALnums;
  atomic<double> ADnums;

  void addLAnums() {
      for(int i = 0; i < 1000000; i++) {
          LAnums.add(2);
      }
  }

  void addDAnums() {
      for(int i = 0; i < 1000000; i++) {
          DAnums.add(3.5);
      }
  }

  void addALnums() {
      for(int i = 0; i < 1000000; i++) {
          ALnums += 2;
      }
  }

  void addADnums() {
      for(int i = 0; i < 1000000; i++) {
          double ret = ADnums.load();
          ADnums.compare_exchange_strong(ret, ret + 3.5);
      } 
  }
  ```

  测试函数测试了LongAdder和atomic<long>及DoubleAdder和atomic<double>四种变量，每种变量开50个线程，每个线程中执行1000000次计数任务，最后打印时间差。具体代码见test.cpp。

* LongAdder 和 atomic<long>

  | 类型         | 线程数量  | 单线程计数  | 预期结果   | 最终结果  | 用时/s   |
  | :----------- | -------- | ---------- | --------- | --------- | ------- |
  | LongAdder    | 50       | 1000000    | 100000000 | 100000000 | 4.96778 |
  | atomic<long> | 50       | 1000000    | 100000000 | 100000000 | 7.89109 |

* DoubleAdder 和 atomic<double>（**c++语言不支持浮点原子算数）**

  | 类型           | 线程数量  | 单线程计数  | 预期结果   | 最终结果    | 用时/s   |
  | :------------- | -------- | ---------- | --------- | ----------- | ------- |
  | DoubleAdder    | 50       | 1000000    | 175000000 | 1.75e+08    | 5.25839 |
  | atomic<double> | 50       | 1000000    | 175000000 | 3.03284e+07 | 9.80220 |
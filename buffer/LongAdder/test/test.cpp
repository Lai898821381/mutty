#include "LongAdder.h"
#include "DoubleAdder.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <ctime>
using namespace buffer;
using namespace std;

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

int main() {
    clock_t startTime,endTime;
    startTime = clock();
    //创建和等待多个线程
    vector<thread> mythreads1, mythreads2, mythreads3, mythreads4;

    for (int i = 0; i < 50; i++)
    {
        mythreads1.push_back(thread(addLAnums));
    }

    for (auto iter = mythreads1.begin(); iter != mythreads1.end(); iter++)
    {
        iter->join();
    }

    endTime = clock();//计时结束
    cout << "The run time of longadder is:" <<(double)(endTime - startTime) / CLOCKS_PER_SEC << "s  nums: " << LAnums.sum()<< endl;

    startTime = clock();
    for (int i = 0; i < 50; i++)
    {
        mythreads2.push_back(thread(addALnums));
    }

    for (auto iter = mythreads2.begin(); iter != mythreads2.end(); iter++)
    {
        iter->join();
    }

    endTime = clock();//计时结束
    cout << "The run time of atomiclong is:" <<(double)(endTime - startTime) / CLOCKS_PER_SEC << "s  nums: " << ALnums.load()<< endl;

    startTime = clock();
    for (int i = 0; i < 50; i++)
    {
        mythreads3.push_back(thread(addDAnums));
    }

    for (auto iter = mythreads3.begin(); iter != mythreads3.end(); iter++)
    {
        iter->join();
    }

    endTime = clock();//计时结束
    cout << "The run time of doubleadder is:" <<(double)(endTime - startTime) / CLOCKS_PER_SEC << "s  nums: " << DAnums.sum()<< endl;

    startTime = clock();
    for (int i = 0; i < 50; i++)
    {
        mythreads4.push_back(thread(addADnums));
    }

    for (auto iter = mythreads4.begin(); iter != mythreads4.end(); iter++)
    {
        iter->join();
    }

    endTime = clock();//计时结束
    cout << "The run time of atomicdouble is:" <<(double)(endTime - startTime) / CLOCKS_PER_SEC << "s  nums: " << ADnums.load()<< endl;
    return 0;
}


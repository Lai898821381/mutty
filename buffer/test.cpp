#include <iostream>
#include <algorithm>
#include <string.h>
#include <chrono>
#include <cstdlib> // Header file needed to use srand and rand
#include <ctime> // Header file needed to use time
#include <pthread.h>
#include <ctime>
#include "PooledByteBuf.h"
#include "PooledByteBufAllocator.h"

using namespace std;
using namespace buffer;

char* toWriteSent = new char[81920];

unsigned seed = time(0);
int alloTimes = 1000000;
int loopTimes = 4;

void testFunc1() {
    srand(seed);
    PooledByteBufAllocator* tt = PooledByteBufAllocator::ALLOCATOR();
    for(int i = 0; i < alloTimes; i++) {
        int size = rand() % 32768;
        PooledByteBuf * toTest = tt->buffer(size);
        // toTest->writeBytes(toWriteSent, size );
        toTest->deallocate();
    }
}

void testFunc2() {
    srand(seed);
    PooledByteBufAllocator* tt = PooledByteBufAllocator::ALLOCATOR();
    for(int i = 0; i < alloTimes * loopTimes; i++) {
        int size = rand() % 32768;
        PooledByteBuf * toTest = tt->buffer(size);
        // toTest->writeBytes(toWriteSent, size );
        toTest->deallocate();
    }
}


int main(int argc, char * argv[]) {
    char* forthSentence = new char[8192];
    fill_n(forthSentence, 8192, '9');
    auto start = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> tm;

    vector<pthread_t> threadArray1, threadArray2;
    fill_n(toWriteSent, 65536, 'X');

    for(int i = 0; i < loopTimes; i++) {
        pthread_t th;
        int ret = pthread_create(&th, nullptr, reinterpret_cast<void *(*)(void *)>(testFunc1), nullptr );
        if(ret != 0) {
            perror("create thread fail");
            exit(1);
        }
        threadArray1.push_back(th);
    }

    start = std::chrono::high_resolution_clock::now();//计时开始
    for(auto& thread : threadArray1) {
        pthread_join(thread, nullptr);
    }

    end = std::chrono::high_resolution_clock::now();//计时结束
    tm = end - start;
    cout << "The run time of PoolByteBUf-multiThread is:" << tm.count() <<  endl;

    pthread_t th;
    int ret = pthread_create(&th, nullptr, reinterpret_cast<void *(*)(void *)>(testFunc2), nullptr );
    if(ret != 0) {
        perror("create thread fail");
        exit(1);
    }
    threadArray2.push_back(th);

    start = std::chrono::high_resolution_clock::now();//计时开始
    for(auto& thread : threadArray2) {
        pthread_join(thread, nullptr);
    }

    end = std::chrono::high_resolution_clock::now();//计时结束
    tm = end - start;
    cout << "The run time of PoolByteBUf-singleThread is:" << tm.count() <<  endl;

    for(int i = 0; i < 10; i++) {
        PooledByteBuf* testSequence =  PooledByteBufAllocator::ALLOCATOR()->buffer(2048);
        testSequence->writeBytes(toWriteSent, 128);
        cout <<"peek " << testSequence->peek() <<" tmpBuf "<< testSequence->tmpBuf() <<" readableBytes " <<testSequence->readableBytes()
        <<" offset "<< testSequence->offset()<<endl;
    }

}

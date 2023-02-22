#include <iostream>
#include <thread>
#include <atomic>

#include "Timer.h"
#include "delay_queue/TimeEntry.h"
using namespace std;
using namespace timer;

static m_timeval max_diff;
static atomic<long> impleTaskNum = {0};

void printSth(m_timeval timeDue)
{
    m_timeval hello;
    hello.getTimeOfDay();
    max_diff = max_diff < (hello - timeDue) ? (hello - timeDue) : max_diff;
    impleTaskNum++;
}

void task(Timer* timer)
{
    m_timeval tt({4,0});
    m_timeval due;
    due.getTimeOfDay();
    due = (due + tt);
    for(int i=0; i< 33334; i++) {
       timer->addByDelay(tt, printSth, due);
    }
}

int main()
{
    m_timeval tick = {{0, 200000}};
    Timer timer(tick, 20);
    timer.start();
    cout<<"now set the task"<<endl;
    thread l1(task, &timer);
    thread l2(task, &timer);
    thread l3(task, &timer);
    l1.join();
    l2.join();
    l3.join();
    cin.get();
    cout <<" totally "<<impleTaskNum<<" tasks have been implemented"<<endl;
    cout << " max_diff "<<max_diff<<endl;
    timer.shutdown();
}

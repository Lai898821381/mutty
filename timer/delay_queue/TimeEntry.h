#pragma once
#include <sstream>
#include "sys/time.h"
using namespace std;
struct m_timeval{
  timeval __node;

  m_timeval(void) {
    this->__node = {-1L, -1L};
  }

  m_timeval(long u) { 
    __node.tv_sec = u / 1000000;
    __node.tv_usec = u % 1000000;
  }

  m_timeval(const timeval& t) {
    __node = t;
  } 

  m_timeval(const m_timeval& val) {
    this->__node = val.__node;
}

  friend bool operator>(m_timeval lval, m_timeval rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    long rsum = rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    return lsum > rsum;
  }

  friend bool operator<(m_timeval lval, m_timeval rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    long rsum = rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    return lsum < rsum;
  }

  friend bool operator>=(m_timeval lval, m_timeval rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    long rsum = rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    return lsum >= rsum;
  }

  friend bool operator!=(m_timeval lval, m_timeval rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    long rsum = rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    return lsum != rsum;
  }

  friend const m_timeval operator%(const m_timeval& lval, const m_timeval& rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    long rsum = rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    timeval ans = {lsum % rsum / 1000000, lsum % rsum % 1000000};
    m_timeval time_ans(ans);
    return time_ans;
  }

  friend const m_timeval operator*(const m_timeval& lval, const long& rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    timeval ans = {lsum * rval / 1000000, lsum * rval % 1000000};
    m_timeval time_ans(ans);
    return time_ans;
  }

  friend const long operator/(const m_timeval& lval, const m_timeval& rval) {
    long lsum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec;
    long rsum = rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    return lsum / rsum;
  }

  friend ostream& operator<<(ostream &os, const m_timeval& _val){
    os << _val.__node.tv_sec << "s " << _val.__node.tv_usec << "us";
    return os;
  }

  friend m_timeval operator-(const m_timeval& lval, const m_timeval& rval){
    long sum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec - rval.__node.tv_sec*1000000 - rval.__node.tv_usec;
    if(sum < 0)
        return m_timeval({-1L, -1L});
    timeval ans = {sum/1000000, sum%1000000};
    m_timeval time_ans(ans);
    return time_ans;   
  }

  friend m_timeval operator+(const m_timeval& lval, const m_timeval& rval){
    long sum = lval.__node.tv_sec*1000000 + lval.__node.tv_usec + rval.__node.tv_sec*1000000 + rval.__node.tv_usec;
    timeval ans = {sum/1000000, sum%1000000};
    m_timeval time_ans(ans);
    return time_ans;   
  }

  m_timeval& operator=(const m_timeval& val){
    if(this == &val) return *this;
    this->__node = val.__node;
    return *this;
  }

  void getTimeOfDay(){
    gettimeofday(&this->__node,NULL);
  }

  timeval getTimeval(){
    return this->__node;
  }
  string toString() {
    std::ostringstream oss;
    oss << "second: " << this->__node.tv_sec << "s" << " usecond: " << this->__node.tv_usec <<"us";
    return oss.str();
  }
};
 


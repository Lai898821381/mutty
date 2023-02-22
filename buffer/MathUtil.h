#ifndef BUFFER_MATHUTIL_H
#define BUFFER_MATHUTIL_H

namespace buffer{
    class MathUtil final{
      public:
        static const int DEFAULT_INT_SIZE = 32;
        static const int DEFAULT_INT_SIZE_MINUS_ONE = 31;
        // 算出一个int（32bit）类型数字的二进制形式的前导的零的个数。
        static int numberOfLeadingZeros(int signedVal) {
            unsigned int val = static_cast<unsigned int>(signedVal);
            if (val == 0)
                return DEFAULT_INT_SIZE;
            int n = 1;
            //判断第一个非零值是否高于16位
            if (val >> 16 == 0) { n += 16; val <<= 16; }
            //判断第一个非零值是否高于24位
            if (val >> 24 == 0) { n +=  8; val <<=  8; }
            if (val >> 28 == 0) { n +=  4; val <<=  4; }
            if (val >> 30 == 0) { n +=  2; val <<=  2; }
            n -= (val >> 31);
            return n;
        }
        // PoolThreadCache.java
        static int log2(int val) {
            return DEFAULT_INT_SIZE_MINUS_ONE - numberOfLeadingZeros(val);
        }

        static int nextPowerofTwo(int val) {
            val --;
            val |= val >> 1;
            val |= val >> 2;
            val |= val >> 4;
            val |= val >> 8;
            val |= val >> 16;
            val ++;
            return val;
        }

    };
}

#endif // BUFFER_MATHUTIL_H
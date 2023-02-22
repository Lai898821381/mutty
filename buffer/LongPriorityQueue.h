#ifndef BUFFER_LONGPRIORITYQUEUE_H
#define BUFFER_LONGPRIORITYQUEUE_H

#include <exception>
#include <vector>


namespace buffer{
    class LongPriorityQueue {
        std::vector<long> array;
        int size;

        void lift(int index) {
            int parentIndex;
            while (index > 1 && subord(parentIndex = index >> 1, index)) {
                swap(index, parentIndex);
                index = parentIndex;
            }
        }

        void sink(int index) {
            int child;
            while ((child = index << 1) <= size) {
                if (child < size && subord(child, child + 1)) {
                    child++;
                }
                if (!subord(index, child)) {
                    break;
                }
                swap(index, child);
                index = child;
            }
        }

        bool subord(int a, int b) {
            return array[a] > array[b];
        }

        void swap(int a, int b) {
            long value = array[a];
            array[a] = array[b];
            array[b] = value;
        }
    public:
        explicit LongPriorityQueue():size(0){
            array.resize(9);
        }
        static const int NO_VALUE = -1;

        void offer(long handle) {
            if (handle == NO_VALUE) {
                throw std::exception();
            }
            size++;
            if (size == array.size()) {
                // Grow queue capacity.
                // array = Arrays.copyOf(array, 1 + (array.length - 1) * 2);
                array.resize(1 + (array.size() - 1) * 2);
            }
            array[size] = handle;
            lift(size);
        }

        void remove(long value) {
            for (int i = 1; i <= size; i++) {
                if (array[i] == value) {
                    array[i] = array[size--];
                    lift(i);
                    sink(i);
                    return;
                }
            }
        }

        long peek() {
            if (size == 0) { return NO_VALUE; }
            return array[1];
        }

        long poll() {
            if (size == 0) { return NO_VALUE; }
            long val = array[1];
            array[1] = array[size];
            array[size] = 0;
            size--;
            sink(1);
            return val;
        }

        bool isEmpty() { return size == 0; }
    };

}
#endif //BUFFER_LONGPRIORITYQUEUE_H
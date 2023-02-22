#ifndef DOUBLEADDER_H
#define DOUBLEADDER_H

#include "Striped64.h"
#include <iostream>

namespace buffer{
    class DoubleAdder : public Striped64<double>{
    public:
        DoubleAdder(){};
        ~DoubleAdder(){};
        void add(double x) {
            Cell<double> ** cs; double b, v; int m; Cell<double>* c;
            if ((cs = m_cells) != nullptr || !casBase(b + x, b = m_base.load())) {
                int index = probe;
                bool uncontended = true;
                if (cs == nullptr || (m = nCells - 1) < 0 ||
                    (c = m_cells[index & m]) == nullptr ||
                    !(uncontended = casCellValue(v + x, v = c->value.load(), c->value)))
                    longAccumulate(x, uncontended, index);
            }
        }
        double sum() {
            Cell<double> ** cs = m_cells;
            Cell<double> *c;
            double sum = m_base.load();
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

#endif //DOUBLEADDER_H
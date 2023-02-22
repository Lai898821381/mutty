#ifndef MUTTY_ANY_H
#define MUTTY_ANY_H

#include <iostream>
#include <list>
#include <cassert>

namespace base {

    class any {
    private:
        class placeholder {
        public:
            virtual ~placeholder() {

            }
        public:
            virtual const std::type_info& type() const = 0;
            virtual placeholder* clone() const = 0;
        };

        template<typename ValueType>
        class holder : public placeholder {
        public:
            holder(const ValueType& value): held(value) {

            }
        public:
            virtual const std::type_info& type() const {
                return typeid(ValueType);
            }

            virtual holder* clone() const {
                return new holder(held);
            }
        public:
            ValueType held;
        };

    public:
        any(): content(nullptr) {

        }

        //模板构造函数，参数可以是任意类型，真正的数据保存在content中
        template<typename ValueType>
        any(const ValueType& value): content(new holder<ValueType>(value)) {

        }

        //拷贝构造函数
        any(const any& other): content(other.content ? other.content->clone() : 0) {

        }

        any& operator=(any& other) {
            return swap(other);
        }

        any& operator=(const any& other) {
            content = other.content->clone();
            return *this;
        }

        const std::type_info& type() const {
            return content ? content->type() : typeid(void);
        }

        ~any() {
            if (nullptr != content) {
                delete content;
            }
        }


    private:
        placeholder* content;

        any &swap(any &rhs)
        {
            std::swap(content, rhs.content);
            return *this;
        }

//        template<typename ValueType>
//        friend ValueType any_cast(const any& operand);

        template<typename ValueType>
        friend  ValueType* any_cast(any* val);

    };


    template<typename ValueType>
    ValueType* any_cast(any* operand){
        assert(operand->type() == typeid(ValueType));
        return (operand && operand->type() == typeid(ValueType))
               ? &dynamic_cast<any::holder<ValueType>*>(operand->content)->held : nullptr;
    }

}

#endif //MUTTY_ANY_H

#pragma once

namespace thread_pool {
    class NonCopyable {
    protected:
        NonCopyable() {};
        ~NonCopyable() {};

    private:
        NonCopyable(const NonCopyable&);
        NonCopyable& operator=(const NonCopyable&);
    };
}

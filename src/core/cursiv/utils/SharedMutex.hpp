#pragma once

#include <shared_mutex>

namespace cursiv {

// What a reader and a writer lock. MinGW's `std::shared_mutex` stands on a
// winpthreads lock that sets itself up at first use: a thread that loses that
// race gets an error instead of the lock, and the standard type drops the
// error. So the lock is used once here, while no other thread can see it.
#if defined(__MINGW32__) && defined(__GLIBCXX__)
class SharedMutex : public std::shared_mutex {
 public:
  SharedMutex() {
    lock_shared();
    unlock_shared();
  }
};
#else
using SharedMutex = std::shared_mutex;
#endif

}  // namespace cursiv

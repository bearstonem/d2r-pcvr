#pragma once
#ifdef _WIN32
#include <windows.h>
namespace framevr {
// Avoid MinGW winpthreads' process-wide bookkeeping on hot callback locks.
// Nonrecursive, matching std::mutex/std::shared_mutex at these call sites.
class NativeMutex {
    SRWLOCK value=SRWLOCK_INIT;
public:
    void lock(){AcquireSRWLockExclusive(&value);}
    void unlock(){ReleaseSRWLockExclusive(&value);}
    bool try_lock(){return TryAcquireSRWLockExclusive(&value)!=0;}
    void lock_shared(){AcquireSRWLockShared(&value);}
    void unlock_shared(){ReleaseSRWLockShared(&value);}
    bool try_lock_shared(){return TryAcquireSRWLockShared(&value)!=0;}
    NativeMutex()=default;
    NativeMutex(const NativeMutex&)=delete;
    NativeMutex& operator=(const NativeMutex&)=delete;
};
// For callbacks that release retained COM objects: their final Release may
// synchronously reenter resource-destruction bookkeeping on this thread.
class NativeRecursiveMutex {
    CRITICAL_SECTION value;
public:
    NativeRecursiveMutex(){InitializeCriticalSection(&value);}
    ~NativeRecursiveMutex(){DeleteCriticalSection(&value);}
    void lock(){EnterCriticalSection(&value);}
    void unlock(){LeaveCriticalSection(&value);}
    NativeRecursiveMutex(const NativeRecursiveMutex&)=delete;
    NativeRecursiveMutex& operator=(const NativeRecursiveMutex&)=delete;
};
}
#else
#include <shared_mutex>
#include <mutex>
namespace framevr {using NativeMutex=std::shared_mutex;using NativeRecursiveMutex=std::recursive_mutex;}
#endif

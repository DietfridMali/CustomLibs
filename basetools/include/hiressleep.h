#pragma once

#include <stdint.h>
#include "basesingleton.hpp"

#ifdef _WIN32
#   include <windows.h>
#   ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#       define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#   endif
#endif

// =================================================================================================

class HiresSleep 
    : public BaseSingleton<HiresSleep>
{
public:
    HiresSleep() {
#ifdef _WIN32
        h_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (not h_) h_ = CreateWaitableTimerW(nullptr, TRUE, nullptr);
#endif
    }

    ~HiresSleep() {
#ifdef _WIN32
        if (h_) { CloseHandle(h_); h_ = nullptr; }
#endif
    }

    // Monotone Zeit in Nanosekunden
    static int64_t GetTime(void) {
#ifdef _WIN32
        LARGE_INTEGER c, f;
        QueryPerformanceCounter(&c);
        QueryPerformanceFrequency(&f);
        return (int64_t)((c.QuadPart * 1000000000LL + f.QuadPart / 2) / f.QuadPart); // rundend
#else
        timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
    }



    // Schlafe bis absolute Deadline (ns, CLOCK_MONOTONIC-Basis)
    void SleepTo(int64_t deadline) const {
#ifdef _WIN32
        int64_t diff = deadline - GetTime();
        if (diff <= 0) return;
        LARGE_INTEGER due;
        due.QuadPart = -((diff + 99) / 100);            // 100-ns Ticks, rundend, relativ
        if (h_) {
            if (SetWaitableTimer(h_, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(h_, INFINITE);
                return;
            }
        }
        Sleep((DWORD)((diff + 999999) / 1000000));      // Fallback
#else
#ifdef TIMER_ABSTIME
        timespec ts;
        ts.tv_sec = deadline / 1000000000LL;
        ts.tv_nsec = deadline % 1000000000LL;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
#else
        int64_t diff = deadline - GetTime();
        if (diff <= 0) return;
        timespec rq{ diff / 1000000000LL, diff % 1000000000LL };
        while (nanosleep(&rq, &rq) == -1 and errno == EINTR) {}
#endif
#endif
    }

    // Relativ schlafen
    void Sleep(int64_t duration) const { 
        SleepTo(GetTime() + duration); 
    }
    
    void SleepMS(int64_t duration) const { 
        Sleep(duration * 1000000LL); 
    }

private:
#ifdef _WIN32
    HANDLE h_{ nullptr };
#endif
};

#define hiresSleep HiresSleep::Instance()

// =================================================================================================

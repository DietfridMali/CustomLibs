#pragma once

#include "std_defines.h"
#if 0
#   include <windows.h>
#   include <stdio.h>
#endif

#include "SDL.h"
#include "hiressleep.h"
#include "conversions.hpp"

#include <algorithm>

// ================================================================================================ =
// High-Resolution Timer (intern/extern: micro seconds, int64_t)

class HiresTimer {
protected:
    int64_t m_startTime;
    int64_t m_endTime;
    int64_t m_lapTime;
    int64_t m_duration;
    int64_t m_slack;

public:
    static inline int64_t Upscale(int t) noexcept {
        return int64_t(t) * 1000;
    }


    static inline int Downscale(int64_t t) noexcept {
        return int((t + 500) / 1000);
    }


    static inline int64_t BaseTime() {
        static int64_t baseTime = int64_t(SDL_GetPerformanceCounter());
        return baseTime;
    }


    static inline int64_t Frequency() {
        static int64_t frequency = int64_t(SDL_GetPerformanceFrequency());
        return frequency;
    }


    static inline int64_t GetHiresTime() noexcept { // micro seconds
        const int64_t t = int64_t(SDL_GetPerformanceCounter()) - BaseTime();
        const int64_t f = Frequency();
        const int64_t q = t / f;      // Sekunden
        const int64_t r = t % f;      // Rest-Ticks
        return q * 1000000 + (r * 1000000 + f / 2) / f; // rundet
    }


    HiresTimer(int64_t duration = 0)
        : m_startTime(0)
        , m_endTime(0)
        , m_lapTime(0)
        , m_duration(duration)
        , m_slack(0)
    {
    }


    inline void SetDuration(int64_t duration)
        noexcept
    {
        m_duration = duration;
    }


    int64_t Start(int64_t offset = 0)
        noexcept
    {
        m_startTime = GetHiresTime() + offset;
        m_endTime = m_startTime + m_duration;
        return m_startTime;
    }


    inline int64_t TakeLapTime(void)
        noexcept
    {
        return m_lapTime = GetHiresTime() - m_startTime;
    }


    bool HasExpired(int64_t time = 0, bool restart = false)
        noexcept
    {
        TakeLapTime();
        if ((m_startTime > 0) and (m_lapTime < (time ? time : m_duration)))
            return false;
        if (restart)
            Start();
        return true;
    }


    inline int64_t StartTime(void)
        const noexcept
    {
        return m_startTime;
    }


    inline int64_t EndTime(void)
        const noexcept
    {
        return m_endTime;
    }


    inline void SetStartTime(int64_t startTime) noexcept {
        m_startTime = startTime;
    }

    inline int64_t Duration(void)
        const noexcept
    {
        return m_duration;
    }


    inline int64_t LapTime(void)
        const noexcept
    {
        return m_lapTime;
    }


    inline int64_t RemainingTime(void)
        noexcept
    {
        return m_duration - TakeLapTime();
    }


    inline float Progress(bool clamped = false)
        noexcept
    {
        float progress = float(TakeLapTime()) / float(m_duration);
        return clamped ? std::clamp(progress, 0.0f, 1.0f) : progress;
    }


    inline bool IsRemaining(int64_t time)
        noexcept
    {
        return RemainingTime() >= time;
    }


    inline void Sleep(int64_t t) {
        if (t > 0)
            hiresSleep.Sleep(t);
    }


    void Delay(void)
        noexcept
    {
        Sleep(m_duration - m_slack - TakeLapTime());
        m_slack = TakeLapTime() - m_duration;
    }

    // compute ramp value derived from current time and timer's start and end times and a threshold value
    inline float Ramp(int64_t threshold, int64_t t = -1) noexcept {
        return Conversions::Rampi((t < 0) ? GetHiresTime() : t, m_startTime, m_endTime, threshold);
    }
};

// =================================================================================================
// Timer functions: Measuring time, delaying program execution, etc.

class Timer 
    : public HiresTimer
{
public:
    static inline int GetTime(void) noexcept {
        return Downscale(GetHiresTime());
    }

    Timer(int duration = 0)
        : HiresTimer(Upscale(duration))
    {
    }


    inline void SetDuration(int duration)
        noexcept
    {
        m_duration = Upscale(duration);
    }


    inline int Start(int offset = 0)
        noexcept
    {
        return Downscale(HiresTimer::Start(Upscale(offset)));
    }


    inline int TakeLapTime(void)
        noexcept
    {
        return Downscale(HiresTimer::TakeLapTime());
    }


    bool HasExpired(int time = 0, bool restart = false)
        noexcept
    {
        return HiresTimer::HasExpired(Upscale(time), restart);
    }


    inline int StartTime(void)
        const noexcept
    {
        return Downscale(m_startTime);
    }


    inline int EndTime(void)
        const noexcept
    {
        return Downscale(m_endTime);
    }


    inline void SetStartTime(int startTime) noexcept {
        m_startTime = Upscale(startTime);
    }

    inline int Duration(void)
        const noexcept
    {
        return Downscale(m_duration);
    }


    inline int LapTime(void)
        const noexcept
    {
        return Downscale(m_lapTime);
    }


    inline int RemainingTime(void)
        noexcept
    {
        return Downscale(HiresTimer::RemainingTime());
    }


    inline float Progress(bool clamped = false)
        noexcept
    {
        return HiresTimer::Progress(clamped);
    }


    inline bool IsRemaining(int time)
        noexcept
    {
        return HiresTimer::IsRemaining(Upscale(time));
    }


    inline void Sleep(int t) {
        if (t > 0)
            hiresSleep.Sleep(Upscale(t));
    }


    // compute ramp value derived from current time and timer's start and end times and a threshold value
    inline float Ramp(int threshold, int t = -1) noexcept {
        return HiresTimer::Ramp(Upscale(threshold), Upscale(t));
    }
};

// =================================================================================================

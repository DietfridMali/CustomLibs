#pragma once

#include "std_defines.h"
#if 0
#   include <windows.h>
#   include <stdio.h>
#endif

#include "SDL.h"
#include "hiressleep.h"
#include "conversions.hpp"

// ================================================================================================ =
// High-Resolution Timer (intern/extern: Millisekunden, int64_t)

class HiresTimer {
public:
    int64_t m_startTime;
    int64_t m_endTime;
    int64_t m_lapTime;
    int64_t m_duration;
    int64_t m_slack;


    static inline int64_t Upscale(int t) noexcept {
        return int64_t(t) * 1000;
    }


    static inline int Downscale(int64_t t) noexcept {
        return int((t + 500) / 1000);
    }


    static inline int64_t Frequency() {
        static int64_t frequency = int64_t(SDL_GetPerformanceFrequency());
        return frequency;
    }


    static inline int64_t GetMS(void) {
        int64_t t = int64_t(SDL_GetPerformanceCounter());
        int64_t f = Frequency();
        int64_t q = t / f;
        int64_t r = t % f;
        return q * 1000 + (r * 1000 + f / 2) / f;
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
        m_startTime = GetMS() + offset;
        m_endTime = m_startTime + offset + m_duration;
        return m_startTime;
    }


    inline int64_t GetLapTime(void)
        noexcept
    {
        return m_lapTime = GetMS() - m_startTime;
    }


    bool HasExpired(int64_t time = 0, bool restart = false)
        noexcept
    {
        GetLapTime();
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
        return m_duration - GetLapTime();
    }


    inline float Progress(void)
        noexcept
    {
        return float(GetLapTime()) / float(m_duration);
    }


    inline bool IsRemaining(int64_t time)
        noexcept
    {
        return RemainingTime() >= time;
    }


    void Delay(void)
        noexcept
    {
        int64_t t = m_duration - m_slack - GetLapTime();
        if (t > 0)
            hiresSleep.Sleep(t);
        m_slack = GetLapTime() - m_duration;
    }

    // compute ramp value derived from current time and timer's start and end times and a threshold value
    inline float Ramp(int64_t threshold, int64_t t = -1) noexcept {
        return Conversions::Ramp(float((t < 0) ? GetMS() : t), float(m_startTime), float(m_endTime), float(threshold));
    }
};

// =================================================================================================
// Timer functions: Measuring time, delaying program execution, etc.

class Timer 
    : public HiresTimer
{
public:
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


    inline int GetLapTime(void)
        noexcept
    {
        return Downscale(HiresTimer::GetLapTime());
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


    inline float Progress(void)
        noexcept
    {
        return HiresTimer::Progress();
    }


    inline bool IsRemaining(int time)
        noexcept
    {
        return Downscale(HiresTimer::IsRemaining(Upscale(time)));
    }


    // compute ramp value derived from current time and timer's start and end times and a threshold value
    inline float Ramp(int threshold, int t = -1) noexcept {
        return HiresTimer::Ramp(Upscale(threshold), Upscale(t));
    }
};

// =================================================================================================

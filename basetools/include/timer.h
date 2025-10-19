#pragma once

#include "std_defines.h"
#if 0
#   include <windows.h>
#   include <stdio.h>
#endif

#include "SDL.h"
#include "hiressleep.h"
#include "conversions.hpp"

// =================================================================================================
// Timer functions: Measuring time, delaying program execution, etc.

class Timer {
public:
    int64_t m_startTime;
    int64_t m_endTime;
    int64_t m_lapTime;
    int64_t m_duration;
    int64_t m_slack;


    static int64_t GetTicks(void) {
        return int64_t(SDL_GetPerformanceCounter());
    }


    static inline int64_t Frequency() {
        static int64_t frequency = int64_t(SDL_GetPerformanceFrequency());
        return frequency;
    }


    static inline int64_t Downscale(int64_t ticks) {
        if (ticks < 0) 
            return -Downscale(-ticks);
        int64_t f = Frequency();
        int64_t q = ticks / f;
        int64_t r = ticks % f;
        return q * 1000 + (r * 1000 + f / 2) / f;
    }


    static inline int64_t Upscale(int64_t ms) {
        if (ms < 0)
            return -Upscale(-ms);
        int64_t f = Frequency();
        int64_t q = ms / 1000;
        int64_t r = ms % 1000;
        return q * f + (r * f + 500) / 1000;
    }


    static inline int64_t TicksToNs(int64_t ticks) {
        if (ticks < 0) 
            return -TicksToNs(-ticks);
        int64_t f = Frequency();
        int64_t q = ticks / f;
        int64_t r = ticks % f;
        return q * 1000000000LL + (r * 1000000000LL + f / 2) / f; // round-to-nearest
    }


    Timer(int duration = 0)
        : m_startTime(0)
        , m_endTime(0)
        , m_lapTime(0)
        , m_duration(duration)
        , m_slack(0)
    {
    }


    inline void SetDuration(int duration)
        noexcept
    {
        m_duration = Upscale(duration);
    }


    int Start(int offset = 0)
        noexcept
    {
        m_startTime = GetTicks() + Upscale(offset);
        m_endTime = m_startTime + offset + m_duration;
        return m_startTime;
    }


    inline int GetLapTime(void)
        noexcept
    {
        return m_lapTime = GetTicks() - m_startTime;
    }


    bool HasExpired(int time = 0, bool restart = false)
        noexcept
    {
        GetLapTime();
        if ((m_startTime > 0) and (m_lapTime < (time ? int64_t(time) : m_duration)))
            return false;
        if (restart)
            Start();
        return true;
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
        return Downscale(m_duration - GetLapTime());
    }


    inline float Progress(void)
        noexcept
    {
        return float(GetLapTime()) / float(m_duration);
    }


    inline bool IsRemaining(int time)
        noexcept
    {
        return Downscale(RemainingTime() >= time);
    }


    void Delay(void)
        noexcept
    {
        int64_t t = m_duration - m_slack - GetLapTime();
        if (t > 0)
            hiresSleep.Sleep(TicksToNs(t));
        m_slack = GetLapTime() - m_duration;
    }

    // compute ramp value derived from current time and timer's start and end times and a threshold value
    inline float Ramp(int threshold, int t = -1) noexcept {
        return Conversions::Ramp(float((t < 0) ? SDL_GetTicks() : t), float(m_startTime), float(m_endTime), float(threshold));
    }
};

// =================================================================================================

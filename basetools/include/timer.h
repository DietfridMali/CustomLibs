#pragma once

#include "std_defines.h"
#include <windows.h>
#include <stdio.h>

#include "SDL.h"

// =================================================================================================
// Timer functions: Measuring time, delaying program execution, etc.

class Timer {
public:
    int  m_startTime;
    int  m_endTime;
    int  m_lapTime;
    int  m_duration;
    int  m_slack;

    Timer(int duration = 0)
        : m_startTime(0), m_endTime(0), m_lapTime(0), m_duration(duration), m_slack(0)
    {
    }


    inline void SetDuration(int duration)
        noexcept
    {
        m_duration = duration;
    }


    int Start(int offset = 0)
        noexcept
    {
        m_startTime = SDL_GetTicks() + offset;
        m_endTime = m_startTime + offset + m_duration;
        return m_startTime;
    }


    inline int GetLapTime(void)
        noexcept
    {
        return m_lapTime = SDL_GetTicks() - m_startTime;
    }


    bool HasExpired(int time = 0, bool restart = false)
        noexcept
    {
        GetLapTime();
        if (time == 0)
            time = m_duration;
        if ((m_startTime > 0) and (m_lapTime < time))
            return false;
        if (restart)
            Start();
        return true;
    }


    inline int StartTime(void)
        noexcept
    {
        return m_startTime;
    }


    inline int EndTime(void)
        noexcept
    {
        return m_endTime;
    }


    inline int LapTime(void)
        noexcept
    {
        return m_lapTime;
    }


    inline int RemainingTime(void)
        noexcept
    {
        return m_duration - GetLapTime();
    }


    inline float Progress(void)
        noexcept
    {
        return float(GetLapTime()) / float(m_duration);
    }


    inline bool IsRemaining(int time)
        noexcept
    {
        return RemainingTime() >= time;
    }


    void Delay(void)
        noexcept
    {
        int t = m_duration - m_slack - GetLapTime();
        if (t > 0)
            Sleep(DWORD(t));
        m_slack = GetLapTime() - m_duration;
    }
};

// =================================================================================================

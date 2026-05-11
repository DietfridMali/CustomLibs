#pragma once

#if !defined(USE_TRACY)
#  define USE_TRACY 0
#endif

#if USE_TRACY
#  ifndef TRACY_ENABLE
#    define TRACY_ENABLE
#  endif
#  include <tracy/Tracy.hpp>
#else
#  define ZoneScoped
#  define ZoneScopedN(x)
#  define ZoneScopedC(c)
#  define ZoneScopedNC(x, c)
#  define FrameMark
#  define FrameMarkNamed(x)
#  define FrameMarkStart(x)
#  define FrameMarkEnd(x)
#  define TracyMessage(text, size)
#  define TracyMessageL(text)
#  define TracyPlot(name, value)
#  define TracyPlotConfig(name, type, step, fill, color)
#endif

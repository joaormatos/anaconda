#include "platform.h"

#ifdef _WIN32
#include "windows.h"
#endif

#include "fpslimit.h"


FPSLimiter::FPSLimiter() : framerate(-1)
{
    old_time = platform_get_time();
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
}

FPSLimiter::~FPSLimiter()
{
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void FPSLimiter::set(int value)
{
    framerate = value;
}

void FPSLimiter::start()
{
    double current_time = platform_get_time();
    next_update = current_time + 1.0 / framerate;
    dt = current_time - old_time;
    old_time = current_time;
    if (dt < 0.0)
        dt = 0.001;
    current_framerate = 1.0 / dt;
}

void FPSLimiter::finish()
{
    if (framerate >= 100)
        return;
    platform_sleep(next_update - platform_get_time());
}
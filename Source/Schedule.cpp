#include "Schedule.h"
#include <cmath>

double Schedule::positionAt (double ppq) const
{
    const auto bar = barLengthPpq();
    const auto length = lengthBars * bar;
    auto offset = ppq - (startBar - 1) * bar;

    if (offset < 0.0 || length <= 0.0)
        return -1.0;

    if (repeat)
        offset = std::fmod (offset, length + gapBars * bar);

    return offset < length ? offset / length : -1.0;
}

double Schedule::loopPositionAt (double ppq) const
{
    const auto bar = barLengthPpq();
    const auto length = lengthBars * bar;

    if (length <= 0.0)
        return -1.0;

    auto offset = std::fmod (ppq, length + gapBars * bar);

    if (offset < 0.0)
        offset += length + gapBars * bar;

    return offset < length ? offset / length : -1.0;
}

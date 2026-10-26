#pragma once

// When the message plays, in the host's musical time.
struct Schedule
{
    int startBar = 1;        // 1-based
    int lengthBars = 2;
    bool repeat = false;
    int gapBars = 0;         // silence between repeats
    int beatsPerBar = 4;     // time signature numerator
    int beatUnit = 4;        // time signature denominator

    double barLengthPpq() const { return beatsPerBar * 4.0 / beatUnit; }

    // Where in the message this moment falls, from 0 (start) to just under 1 (end),
    // or -1 when the message isn't playing.
    double positionAt (double ppq) const;

    // Same, for a free-running loop that ignores the start bar and always repeats.
    double loopPositionAt (double ppq) const;
};

#pragma once

#include <stdint.h>

// Turns one sweep of the IR transmit power into a single number: how much
// weaker than full power the transmitter can get while the receiver still
// hears its echo.
//
// The receiver demodulates a 38 kHz carrier and only says "carrier" or "no
// carrier", so it cannot report how bright an echo is. Instead the firmware
// sends bursts at kLevels powers, from the weakest to the strongest, and
// counts how many bursts of each the receiver heard. A near or bright target
// is heard at low power; the margin in dB grows as the target comes closer.
//
// The power is set by the carrier's duty cycle d (out of kFullScale). The
// receiver filters out everything but the carrier frequency, and the
// amplitude of that fundamental in a pulse train is proportional to
// sin(pi * d), largest at d = 1/2.
class IrSweep {
  public:
    static constexpr int kLevels = 24;
    // LEDC resolution in bits. With 10 bits the board sets carriers up to
    // about 39 kHz: 40 kHz and up fail with "requested frequency and duty
    // resolution can not be achieved" (checked on 2026-10-04).
    static constexpr int kResolutionBits = 10;
    static constexpr uint32_t kFullScale = 1u << kResolutionBits;

    enum class State : uint8_t {
        Ok,        // heard at some powers and not at others
        NoEcho,    // not heard even at full power: far, dark or nothing there
        Saturated, // heard even at the weakest power: very near, or leakage
        Noisy,     // the receiver "heard" bursts with the LED off
    };

    struct Result {
        State state = State::NoEcho;
        // Full power over the weakest power that is still heard, in dB.
        // 0 for NoEcho, the weakest level's margin for Saturated.
        float marginDb = 0;
        // Where the 50 % point lies, as a fractional level: 0 for Saturated,
        // kLevels - 1 for NoEcho.
        float level = kLevels - 1;
    };

    // A run of levels to measure, first to last inclusive.
    struct Window {
        int first = 0;
        int last = kLevels - 1;

        bool full() const { return first == 0 && last == kLevels - 1; }
    };

    // Levels measured either side of the last threshold.
    static constexpr int kHalfWindow = 3;

    // Duty for level 0 (weakest) .. kLevels - 1 (full, d = 1/2).
    static uint32_t duty(int level);

    // Amplitude of the level relative to full power, in dB: <= 0.
    static float levelDb(int level);

    // hits[i]: how many of `bursts` bursts at level i were heard.
    // falseHits: how many blank bursts, sent with the LED off, were heard.
    // Any of them makes the sweep Noisy: ambient IR pulses (an iPhone's
    // camera close by, for one) then pass for echoes at random levels.
    // A Noisy result still carries the margin the hits would give.
    //
    // The 50 % point is found from the sum of the hit fractions rather than
    // from the first level that is heard, so a single stray hit or miss
    // shifts it a little instead of a whole level. For a clean step (levels
    // j and up heard, below j not) the sum is kLevels - j and the point lands
    // half-way between levels j - 1 and j.
    static Result estimate(const uint8_t hits[kLevels], uint8_t bursts, uint8_t falseHits = 0);

    // A full sweep takes 24 levels; while the target stays put the
    // threshold moves little, so the next sweep only measures kHalfWindow
    // levels either side of it: the bottom of the scale after Saturated,
    // the top after NoEcho, all of it after Noisy.
    static Window around(const Result &last);

    // After measuring only `window`: whether the threshold is inside it,
    // that is its lowest level unheard and its highest heard, unless they
    // are the ends of the scale. If not, the sweep says nothing and a full
    // one is needed.
    static bool contains(const uint8_t hits[kLevels], uint8_t bursts, Window window);

    // Fills the levels outside `window` as a full sweep would have found
    // them if the threshold is inside it: unheard below, all heard above.
    static void fill(uint8_t hits[kLevels], uint8_t bursts, Window window);

  private:
    static Result estimateHits(const uint8_t hits[kLevels], uint8_t bursts);
};

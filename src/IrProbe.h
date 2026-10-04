#pragma once

#include <stddef.h>
#include <stdint.h>

// The StickS3's IR LED (G46) and IR receiver (G42), used as a reflection
// probe: the LED sends short bursts of carrier, and the receiver, which sits
// next to it on the same end of the stick, reports whether it heard them
// back.
//
// The receiver demodulates the carrier: its output goes low while it hears
// one. Its automatic gain control suppresses a carrier that never stops, so
// the bursts are short and separated by longer gaps.
//
// Both parts are powered from EXT_5V: begin() turns the PMIC's 5 V output
// on. The speaker amplifier disturbs the receiver, so setup() must leave
// the speaker off (cfg.internal_spk = false).
class IrProbe {
  public:
    // Call after M5.begin().
    void begin(uint32_t carrierHz);

    // Returns the carrier the LEDC actually set, or 0 if it could not.
    uint32_t setCarrier(uint32_t hz);
    uint32_t carrier() const { return carrierHz_; }

    // Sends `bursts` bursts with the given duty (out of IrSweep::kFullScale)
    // and returns how many of them the receiver heard. A burst takes 1.3 ms
    // plus the gap after it.
    uint8_t hits(uint32_t duty, uint8_t bursts);

    // One burst while the receiver's output is sampled every `stepUs`:
    // out[i] is '0' or '1', `count` samples from the burst's start. For
    // studying how the receiver answers; blocks for count * stepUs.
    void wave(uint32_t duty, uint32_t stepUs, char *out, size_t count);

    // Pause after every burst, in microseconds: `heard` after one the
    // receiver heard, `missed` after one it did not. The receiver's gain
    // control lowers its gain after every burst it hears and takes about
    // 15 ms to recover: with a paper sheet at 10 cm the margin read 15.9 dB
    // with 1.6 ms gaps, 18.6 with 5 ms and 23.1 with 15, 30 and 60 ms
    // (checked on 2026-10-04). Both are 15 ms by default, as in the sweeps
    // the calibration came from. A shorter `missed` gap would make the
    // levels below the threshold quick, but whether the gain control reacts
    // to a carrier too weak to be heard is not settled: with a hand-held
    // sheet 2 ms looked 1-2 dB less sensitive, within the hand's wobble.
    void setGap(uint32_t heard, uint32_t missed) {
        gapUs_ = heard;
        missGapUs_ = missed;
    }
    uint32_t gap() const { return gapUs_; }
    uint32_t missGap() const { return missGapUs_; }

  private:
    bool burst(uint32_t duty);

    uint32_t carrierHz_ = 0;
    uint32_t gapUs_ = 15000;
    uint32_t missGapUs_ = 15000;
};

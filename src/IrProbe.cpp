#include "IrProbe.h"

#include <M5Unified.h>

#include "IrSweep.h"

namespace {

constexpr uint8_t kTxPin = 46;
constexpr uint8_t kRxPin = 42;
// The display backlight takes channel 7 (timer 3); channel 0 has timer 0.
constexpr uint8_t kChannel = 0;

// A burst of about 30 carrier cycles: demodulating receivers want at least
// 10 before they answer, and answer 7 to 15 cycles (up to 400 us) late.
constexpr uint32_t kBurstUs = 800;
// The receiver's output is sampled every 10 us during the first 1.1 ms
// from the burst's start, and an echo holds it low for at least 300 us of
// that. A weak echo near the threshold measured 740 us, a strong one with
// the gain control's gap 580 us (checked on 2026-10-04 with `wave`).
constexpr uint32_t kSampleStepUs = 10;
constexpr uint32_t kWindowUs = 1100;
constexpr uint32_t kMinLowUs = 300;
// The output can stay low up to 260 us after the burst ends; after this it
// must be idle again.
constexpr uint32_t kTailUs = 500;
// The output must be idle before a burst, or a hit would be ambient noise.
constexpr uint32_t kIdleWaitUs = 2000;

} // namespace

void IrProbe::begin(uint32_t carrierHz) {
    M5.Power.setExtOutput(true);

    pinMode(kRxPin, INPUT_PULLUP);

    // ledcReadFreq() would answer 0 while the duty is 0, so the frequency
    // is kept from what ledcSetup() returns.
    carrierHz_ = ledcSetup(kChannel, carrierHz, IrSweep::kResolutionBits);
    ledcAttachPin(kTxPin, kChannel);
    ledcWrite(kChannel, 0);
}

uint32_t IrProbe::setCarrier(uint32_t hz) {
    ledcWrite(kChannel, 0);
    const uint32_t actual = ledcChangeFrequency(kChannel, hz, IrSweep::kResolutionBits);
    if (actual != 0) {
        carrierHz_ = actual;
    }
    return actual;
}

bool IrProbe::burst(uint32_t duty) {
    const uint32_t waitFrom = micros();
    while (digitalRead(kRxPin) == LOW) {
        if (micros() - waitFrom > kIdleWaitUs) {
            return false; // the receiver is busy with something else
        }
    }

    // An echo holds the output low from 150 to 400 us into the burst until
    // up to 260 us after its end. A strong one makes the receiver's gain
    // control lift the output for about 250 us in between, so no single
    // moment is sure to be low; the total time low is counted instead.
    // Ambient pulses (lamps) are short, and an echo must be over by the time
    // the tail has passed.
    const uint32_t start = micros();
    ledcWrite(kChannel, duty);
    bool on = true;
    uint32_t lowSamples = 0;
    for (uint32_t at = 0; at < kWindowUs; at += kSampleStepUs) {
        while (micros() - start < at) {
        }
        if (on && at >= kBurstUs) {
            ledcWrite(kChannel, 0);
            on = false;
        }
        if (digitalRead(kRxPin) == LOW) {
            ++lowSamples;
        }
    }
    while (micros() - start < kBurstUs + kTailUs) {
    }
    const bool heard =
        lowSamples * kSampleStepUs >= kMinLowUs && digitalRead(kRxPin) == HIGH;
    delayMicroseconds(heard ? gapUs_ : missGapUs_);
    return heard;
}

uint8_t IrProbe::hits(uint32_t duty, uint8_t bursts) {
    uint8_t heard = 0;
    for (uint8_t i = 0; i < bursts; ++i) {
        if (burst(duty)) {
            ++heard;
        }
    }
    return heard;
}

void IrProbe::wave(uint32_t duty, uint32_t stepUs, char *out, size_t count) {
    const uint32_t start = micros();
    ledcWrite(kChannel, duty);
    bool on = true;
    for (size_t i = 0; i < count; ++i) {
        while (micros() - start < i * stepUs) {
        }
        if (on && micros() - start >= kBurstUs) {
            ledcWrite(kChannel, 0);
            on = false;
        }
        out[i] = digitalRead(kRxPin) == LOW ? '0' : '1';
    }
    ledcWrite(kChannel, 0);
}

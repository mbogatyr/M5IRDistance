#include "Renderer.h"

namespace {

// Portrait: 135 wide, 240 tall, KEY1 below the screen. 2 is the same turned
// by 180°.
constexpr uint8_t kRotation = 0;
constexpr uint8_t kFlippedRotation = 2;

} // namespace

bool Renderer::begin() {
    M5.Display.setRotation(kRotation);
    M5.Display.fillScreen(TFT_BLACK);

    // 135*240*2 = 65 KB, which fits in internal RAM. Code that reads or
    // writes single pixels runs about twice as fast there as through the
    // PSRAM cache, so PSRAM is only the fallback.
    canvas_.setColorDepth(16);
    canvas_.setPsram(false);
    if (canvas_.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
        canvas_.setPsram(true);
        if (canvas_.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
            return false;
        }
    }
    return true;
}

void Renderer::invalidate() { hasPrevious_ = false; }

void Renderer::setFlipped(bool flipped) {
    if (flipped == flipped_) {
        return;
    }
    flipped_ = flipped;
    M5.Display.setRotation(flipped ? kFlippedRotation : kRotation);
    invalidate();
}

void Renderer::draw(const Screen &screen) {
    if (hasPrevious_ && previous_ == screen) {
        return;
    }

    paint(screen);

    hasPrevious_ = true;
    previous_ = screen;
}

void Renderer::writeSnapshot(Print &out) {
    out.printf("SNAP %d %d\n", canvas_.width(), canvas_.height());
    // A 16-bit LovyanGFX sprite already keeps its pixels high byte first,
    // so the buffer goes out as it is.
    out.write(static_cast<const uint8_t *>(canvas_.getBuffer()),
              canvas_.width() * canvas_.height() * 2);
    out.flush();
}

void Renderer::paint(const Screen &screen) {
    const int width = canvas_.width();
    const int centerX = width / 2;

    canvas_.fillSprite(TFT_BLACK);
    canvas_.setTextDatum(middle_center);

    canvas_.setFont(&fonts::Font2);
    canvas_.setTextColor(TFT_DARKGREY);
    canvas_.drawString("IR range", centerX, 14);

    char margin[16];
    snprintf(margin, sizeof(margin), "%d.%d dB", screen.marginDb10 / 10, screen.marginDb10 % 10);

    // A word instead of a number.
    const auto say = [&](const char *text, uint16_t color) {
        canvas_.setFont(&fonts::Font4);
        canvas_.setTextColor(color);
        canvas_.drawString(text, centerX, 70);
    };
    // The big number and the small lines under it.
    const auto number = [&](int tenths, uint16_t color, const char *unit, const char *note) {
        char text[12];
        snprintf(text, sizeof(text), "%d.%d", tenths / 10, tenths % 10);
        canvas_.setFont(&fonts::Font7);
        canvas_.setTextColor(color);
        canvas_.drawString(text, centerX, 64);
        canvas_.setFont(&fonts::Font2);
        canvas_.setTextColor(TFT_DARKGREY);
        canvas_.drawString(unit, centerX, 102);
        if (note != nullptr) {
            canvas_.drawString(note, centerX, 120);
        }
    };

    if (!screen.measured) {
        say("...", TFT_DARKGREY);
    } else if (screen.state == IrSweep::State::NoEcho) {
        say("NO ECHO", TFT_SKYBLUE);
    } else if (screen.state == IrSweep::State::Noisy) {
        say("IR NOISE", TFT_YELLOW);
    } else if (screen.state == IrSweep::State::Saturated) {
        say("TOO NEAR", TFT_ORANGE);
        canvas_.setFont(&fonts::Font2);
        canvas_.setTextColor(TFT_DARKGREY);
        canvas_.drawString(margin, centerX, 102);
    } else if (screen.cm10 >= 0) {
        // Outside the calibrated distances it is a guess: orange.
        number(screen.cm10, screen.extrapolated ? TFT_ORANGE : TFT_WHITE,
               screen.extrapolated ? "cm, extrapolated" : "cm", margin);
    } else {
        number(screen.marginDb10, TFT_WHITE, "dB", screen.calibrated ? nullptr : "not calibrated");
    }

    // One bar per power level, weakest on the left; full height when every
    // burst was heard.
    constexpr int kBarTop = 135;
    constexpr int kBarHeight = 70;
    constexpr int kStep = 5;
    const int left = (width - IrSweep::kLevels * kStep) / 2;
    canvas_.drawFastHLine(left - 1, kBarTop + kBarHeight, IrSweep::kLevels * kStep + 1,
                          TFT_DARKGREY);
    if (screen.measured && screen.bursts > 0) {
        for (int i = 0; i < IrSweep::kLevels; ++i) {
            const int h = kBarHeight * screen.hits[i] / screen.bursts;
            const bool heard = 2 * screen.hits[i] >= screen.bursts;
            canvas_.fillRect(left + i * kStep, kBarTop + kBarHeight - h, kStep - 1, h,
                             heard ? TFT_GREEN : TFT_DARKGREEN);
        }
    }
    canvas_.setFont(&fonts::Font0);
    canvas_.setTextDatum(top_left);
    canvas_.drawString("-50 dB", left, kBarTop + kBarHeight + 4);
    canvas_.setTextDatum(top_right);
    canvas_.drawString("full", left + IrSweep::kLevels * kStep, kBarTop + kBarHeight + 4);

    canvas_.setTextDatum(middle_center);
    canvas_.setFont(&fonts::Font2);
    if (screen.paused) {
        canvas_.setTextColor(TFT_YELLOW);
        canvas_.drawString("PAUSED", centerX, canvas_.height() - 14);
    } else if (screen.battery >= 0) {
        canvas_.setTextColor(TFT_DARKGREY);
        canvas_.drawString(String(screen.battery) + " %", centerX, canvas_.height() - 14);
    }

    canvas_.pushSprite(0, 0);
}

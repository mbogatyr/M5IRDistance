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
    const int centerX = canvas_.width() / 2;
    const int centerY = canvas_.height() / 2;

    canvas_.fillSprite(TFT_BLACK);
    canvas_.setTextDatum(middle_center);
    canvas_.setTextColor(TFT_WHITE);

    canvas_.setFont(&fonts::Font2);
    canvas_.drawString("M5StickS3", centerX, centerY - 30);

    canvas_.setFont(&fonts::Font4);
    canvas_.drawString(String(screen.seconds) + " s", centerX, centerY + 10);

    if (screen.battery >= 0) {
        canvas_.setFont(&fonts::Font2);
        canvas_.setTextColor(TFT_DARKGREY);
        canvas_.drawString(String(screen.battery) + " %", centerX, canvas_.height() - 20);
    }

    canvas_.pushSprite(0, 0);
}

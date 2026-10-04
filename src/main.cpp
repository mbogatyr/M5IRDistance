#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "BatteryFilter.h"
#include "DisplayTimeout.h"
#include "IrProbe.h"
#include "IrSweep.h"
#include "Orientation.h"
#include "PickupDetector.h"
#include "Renderer.h"

namespace {

constexpr uint8_t kBrightness = 120;
constexpr uint32_t kLoopMs = 20;           // 50 Hz
constexpr uint32_t kBatteryPeriodMs = 5000; // the level changes slowly
constexpr uint32_t kCarrierHz = 38000;      // what the receiver is tuned to
constexpr uint8_t kBursts = 4;              // per power level
constexpr uint8_t kBlankBursts = 8;         // with the LED off, per sweep

Renderer renderer;
DisplayTimeout displayTimeout;
Orientation orientation(Orientation::Layout::Portrait);
PickupDetector pickupDetector;
BatteryFilter battery;
IrProbe ir;

bool displayAwake = true;
bool paused = false; // KEY1 stops and restarts the measuring
int batteryShown = -1;

// The sweep in progress: one power level is measured per pass of loop().
uint8_t bursts = kBursts;
uint8_t sweepHits[IrSweep::kLevels];
int sweepLevel = 0;
// The last finished sweep.
uint8_t lastHits[IrSweep::kLevels];
uint8_t lastBursts = 0;
uint32_t lastNoise = 0;
IrSweep::Result lastResult;
bool measured = false;
uint32_t batteryReadMs = 0;

// Self-testing over serial: line commands, see CLAUDE.md.
char command[32];
size_t commandLength = 0;
uint8_t pendingKeys = 0; // presses from k1 / k2: bit 0 KEY1, bit 1 KEY2
bool accelOn = false;
uint32_t accelPrintedMs = 0;
bool irOn = false; // an IR line after every sweep

void setDisplayAwake(bool awake) {
    if (awake == displayAwake) {
        return;
    }
    displayAwake = awake;

    if (awake) {
        M5.Display.wakeup();
        M5.Display.setBrightness(kBrightness);
        renderer.invalidate();
        // The stick may have been turned over while the display was off.
        orientation.reset();
    } else {
        // The backlight is the main power consumer, so it is turned off
        // separately from putting the panel itself to sleep.
        M5.Display.setBrightness(0);
        M5.Display.sleep();
        // The pose the stick is left in is the one a pickup is measured from.
        pickupDetector.reset();
    }
}

void readBattery(uint32_t now, bool force) {
    if (!force && now - batteryReadMs < kBatteryPeriodMs) {
        return;
    }
    batteryReadMs = now;
    const int32_t level = M5.Power.getBatteryLevel();
    if (level >= 0) {
        batteryShown = battery.update(static_cast<uint8_t>(level > 100 ? 100 : level));
    }
}

// Nobody may be reading the port any more: then the USB buffer fills up and
// every print would stall the loop, so periodic lines are skipped instead.
bool serialHasRoom() { return Serial.availableForWrite() >= 128; }

void printStatus() {
    Serial.printf("ST board=%d awake=%d flipped=%d battery=%d%% %dmV internal=%u psram=%u\n",
                  static_cast<int>(M5.getBoard()), displayAwake ? 1 : 0,
                  orientation.flipped() ? 1 : 0, batteryShown,
                  static_cast<int>(M5.Power.getBatteryVoltage()),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

const char *stateName(IrSweep::State state) {
    switch (state) {
    case IrSweep::State::Ok:
        return "ok";
    case IrSweep::State::Saturated:
        return "saturated";
    case IrSweep::State::Noisy:
        return "noisy";
    default:
        return "noecho";
    }
}

void printIr() {
    Serial.printf("IR db=%.1f state=%s noise=%u\n", lastResult.marginDb,
                  stateName(lastResult.state), static_cast<unsigned>(lastNoise));
}

void printSweep() {
    Serial.printf("SW f=%u n=%u gap=%u noise=%u db=%.1f state=%s hits=",
                  static_cast<unsigned>(ir.carrier()), lastBursts,
                  static_cast<unsigned>(ir.gap()),
                  static_cast<unsigned>(lastNoise), lastResult.marginDb,
                  stateName(lastResult.state));
    for (int i = 0; i < IrSweep::kLevels; ++i) {
        Serial.printf(i == 0 ? "%u" : ",%u", lastHits[i]);
    }
    Serial.print('\n');
}

void finishSweep() {
    memcpy(lastHits, sweepHits, sizeof(lastHits));
    lastBursts = bursts;
    // Blank bursts, judged like the real ones: ambient IR that would pass
    // for an echo.
    lastNoise = ir.hits(0, kBlankBursts);
    lastResult = IrSweep::estimate(lastHits, lastBursts, static_cast<uint8_t>(lastNoise));
    measured = true;
    sweepLevel = 0;
}

// Measures the next power level; returns true when that finished a sweep.
bool stepSweep() {
    sweepHits[sweepLevel] = ir.hits(IrSweep::duty(sweepLevel), bursts);
    if (++sweepLevel < IrSweep::kLevels) {
        return false;
    }
    finishSweep();
    return true;
}

// A whole sweep at once, about 1.6 s, whatever the display is doing.
void fullSweep() {
    sweepLevel = 0;
    while (!stepSweep()) {
    }
}

void runCommand(const char *line) {
    if (strcmp(line, "s") == 0) {
        renderer.writeSnapshot(Serial);
    } else if (strcmp(line, "k1") == 0) {
        pendingKeys |= 1;
    } else if (strcmp(line, "k2") == 0) {
        pendingKeys |= 2;
    } else if (strcmp(line, "acc") == 0) {
        accelOn = !accelOn;
    } else if (strcmp(line, "st") == 0) {
        printStatus();
    } else if (strcmp(line, "sw") == 0) {
        fullSweep();
        printSweep();
    } else if (strcmp(line, "ir") == 0) {
        irOn = !irOn;
    } else if (strncmp(line, "freq ", 5) == 0) {
        const uint32_t actual = ir.setCarrier(static_cast<uint32_t>(atol(line + 5)));
        sweepLevel = 0;
        Serial.printf("FREQ %u\n", static_cast<unsigned>(actual));
    } else if (strncmp(line, "gap ", 4) == 0) {
        ir.setGap(static_cast<uint32_t>(atol(line + 4)));
        sweepLevel = 0;
        Serial.printf("GAP %u\n", static_cast<unsigned>(ir.gap()));
    } else if (strncmp(line, "wave ", 5) == 0) {
        // 4 ms in 20 us steps; the burst is the first 0.8 ms.
        static char samples[201];
        ir.wave(IrSweep::duty(atoi(line + 5)), 20, samples, 200);
        samples[200] = '\0';
        Serial.printf("WAVE %s\n", samples);
    } else if (strncmp(line, "n ", 2) == 0) {
        const int n = atoi(line + 2);
        bursts = static_cast<uint8_t>(n < 1 ? 1 : n > 32 ? 32 : n);
        sweepLevel = 0;
        Serial.printf("N %u\n", bursts);
    } else if (line[0] != '\0') {
        Serial.printf("ERR unknown '%s'\n", line);
    }
}

void readSerial() {
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch != '\n' && ch != '\r') {
            if (commandLength < sizeof(command) - 1) {
                command[commandLength++] = ch;
            }
            continue;
        }
        command[commandLength] = '\0';
        commandLength = 0;
        runCommand(command);
    }
}

void printAccel(uint32_t now, float ax, float ay, float az) {
    if (!accelOn || now - accelPrintedMs < 100 || !serialHasRoom()) {
        return;
    }
    accelPrintedMs = now;
    Serial.printf("ACC %.2f %.2f %.2f flipped=%d\n", ax, ay, az, orientation.flipped() ? 1 : 0);
}

} // namespace

void setup() {
    auto cfg = M5.config();
    // The speaker amplifier disturbs the IR receiver; without the speaker
    // M5Unified leaves the amplifier off.
    cfg.internal_spk = false;
    M5.begin(cfg);
    // M5Unified leaves Serial alone (its serial_baudrate is 0 by default).
    Serial.begin(115200);

    M5.Display.setBrightness(kBrightness);
    if (!renderer.begin()) {
        Serial.println("ERR no memory for the frame");
    }

    const uint32_t now = millis();
    ir.begin(kCarrierHz);
    Serial.printf("IR carrier %u Hz\n", static_cast<unsigned>(ir.carrier()));

    displayTimeout.begin(now);
    readBattery(now, true);
}

void loop() {
    M5.update();
    readSerial();

    const uint32_t now = millis();

    // Powering off is not handled here: the side button does it by
    // itself on a double press, via the PMIC.
    const bool key1 = M5.BtnA.wasPressed() || (pendingKeys & 1);
    const bool key2 = M5.BtnB.wasPressed() || (pendingKeys & 2);
    pendingKeys = 0;

    float ax = 0, ay = 0, az = 0;
    const bool haveAccel = M5.Imu.getAccel(&ax, &ay, &az);
    // Only a sleeping display looks for a pickup: the stick lying still at
    // any angle must let it go off.
    const bool pickedUp = !displayAwake && haveAccel && pickupDetector.update(ax, ay, az);

    const bool wasAwake = displayAwake;
    const bool activity =
        M5.BtnA.isPressed() || M5.BtnB.isPressed() || key1 || key2 || pickedUp;
    setDisplayAwake(displayTimeout.shouldBeOn(now, activity));

    if (displayAwake) {
        // A press that wakes the display only wakes it.
        if (wasAwake && key1) {
            paused = !paused;
            sweepLevel = 0;
        }
        if (haveAccel) {
            renderer.setFlipped(orientation.update(now, ax, ay, az));
        }
        readBattery(now, false);
    }

    // Measuring keeps the LED busy, so it runs only while someone looks at
    // the display; the serial `sw` measures at any time.
    const bool measuring = displayAwake && !paused;
    if (measuring && stepSweep() && irOn && serialHasRoom()) {
        printIr();
    }

    if (displayAwake) {
        Renderer::Screen screen;
        screen.state = lastResult.state;
        screen.marginDb10 = static_cast<int>(lastResult.marginDb * 10 + 0.5f);
        memcpy(screen.hits, lastHits, sizeof(screen.hits));
        screen.bursts = lastBursts;
        screen.measured = measured;
        screen.paused = paused;
        screen.battery = batteryShown;
        renderer.draw(screen);
    }

    if (haveAccel) {
        printAccel(now, ax, ay, az);
    }

    // A power level takes about 65 ms, which paces the loop by itself.
    delay(measuring ? 1 : kLoopMs);
}

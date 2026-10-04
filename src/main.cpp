#include <M5Unified.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "BatteryFilter.h"
#include "DisplayTimeout.h"
#include "IrProbe.h"
#include "IrSweep.h"
#include "Orientation.h"
#include "PickupDetector.h"
#include "RangeCalibration.h"
#include "Renderer.h"

namespace {

constexpr uint8_t kBrightness = 120;
constexpr uint32_t kLoopMs = 20;           // 50 Hz
constexpr uint32_t kBatteryPeriodMs = 5000; // the level changes slowly
constexpr uint32_t kCarrierHz = 38000;      // what the receiver is tuned to
constexpr uint8_t kBursts = 4;              // per power level
constexpr uint8_t kBlankBursts = 8;         // with the LED off, per sweep
constexpr int kCalSweeps = 5;               // averaged into a calibration point
constexpr int kCalAttempts = 10;            // sweeps tried for them

Renderer renderer;
DisplayTimeout displayTimeout;
Orientation orientation(Orientation::Layout::Portrait);
PickupDetector pickupDetector;
BatteryFilter battery;
IrProbe ir;
RangeCalibration calibration;
Preferences prefs;

bool displayAwake = true;
bool paused = false; // KEY1 stops and restarts the measuring
bool demo = false;   // a made-up sweep is on screen, see `demo`
int batteryShown = -1;

// The sweep in progress: one power level is measured per pass of loop(),
// only those in the window around the last threshold.
uint8_t bursts = kBursts;
uint8_t sweepHits[IrSweep::kLevels];
IrSweep::Window window; // all levels at first
int sweepLevel = 0;
uint32_t sweepStartMs = 0;
// The last finished sweep.
uint8_t lastHits[IrSweep::kLevels];
uint8_t lastBursts = 0;
uint32_t lastNoise = 0;
uint32_t lastSweepMs = 0; // how long it took, including any restart
IrSweep::Result lastResult;
RangeCalibration::Estimate lastRange;
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

// The calibration points live in NVS as an array of RangeCalibration::Point.
void loadCalibration() {
    RangeCalibration::Point points[RangeCalibration::kMaxPoints];
    prefs.begin("ircal", true);
    const size_t size = prefs.getBytes("points", points, sizeof(points));
    prefs.end();
    for (size_t i = 0; i < size / sizeof(points[0]); ++i) {
        calibration.add(points[i].cm, points[i].db);
    }
}

void saveCalibration() {
    RangeCalibration::Point points[RangeCalibration::kMaxPoints];
    for (int i = 0; i < calibration.count(); ++i) {
        points[i] = calibration.point(i);
    }
    prefs.begin("ircal", false);
    prefs.putBytes("points", points, calibration.count() * sizeof(points[0]));
    prefs.end();
}

void printCalibration() {
    for (int i = 0; i < calibration.count(); ++i) {
        Serial.printf("CALP cm=%.1f db=%.1f\n", calibration.point(i).cm, calibration.point(i).db);
    }
    Serial.printf("CALN %d\n", calibration.count());
}

// "cm=-1" while there is no distance: no calibration, or no clean echo.
void printRange() {
    if (lastRange.valid) {
        Serial.printf("cm=%.1f ext=%d", lastRange.cm, lastRange.extrapolated ? 1 : 0);
    } else {
        Serial.print("cm=-1 ext=0");
    }
}

void printIr() {
    Serial.printf("IR db=%.1f state=%s ", lastResult.marginDb, stateName(lastResult.state));
    printRange();
    Serial.printf(" noise=%u ms=%u\n", static_cast<unsigned>(lastNoise),
                  static_cast<unsigned>(lastSweepMs));
}

void printSweep() {
    Serial.printf("SW f=%u n=%u gap=%u noise=%u db=%.1f state=%s ",
                  static_cast<unsigned>(ir.carrier()), lastBursts,
                  static_cast<unsigned>(ir.gap()), static_cast<unsigned>(lastNoise),
                  lastResult.marginDb, stateName(lastResult.state));
    printRange();
    Serial.print(" hits=");
    for (int i = 0; i < IrSweep::kLevels; ++i) {
        Serial.printf(i == 0 ? "%u" : ",%u", lastHits[i]);
    }
    Serial.print('\n');
}

void startSweep(IrSweep::Window next) {
    window = next;
    sweepLevel = next.first;
}

// Returns false when the threshold has left the window: the sweep then
// starts over with all levels.
bool finishSweep() {
    if (!window.full() && !IrSweep::contains(sweepHits, bursts, window)) {
        startSweep(IrSweep::Window());
        return false;
    }
    IrSweep::fill(sweepHits, bursts, window);
    memcpy(lastHits, sweepHits, sizeof(lastHits));
    lastBursts = bursts;
    // Blank bursts, judged like the real ones: ambient IR that would pass
    // for an echo.
    lastNoise = ir.hits(0, kBlankBursts);
    lastResult = IrSweep::estimate(lastHits, lastBursts, static_cast<uint8_t>(lastNoise));
    lastRange = lastResult.state == IrSweep::State::Ok ? calibration.estimate(lastResult.marginDb)
                                                       : RangeCalibration::Estimate();
    const uint32_t now = millis();
    lastSweepMs = now - sweepStartMs;
    sweepStartMs = now;
    measured = true;
    startSweep(IrSweep::around(lastResult));
    return true;
}

// Measures the next power level; returns true when that finished a sweep.
bool stepSweep() {
    sweepHits[sweepLevel] = ir.hits(IrSweep::duty(sweepLevel), bursts);
    if (++sweepLevel <= window.last) {
        return false;
    }
    return finishSweep();
}

// A sweep over all levels at once, whatever the display is doing. It is
// what `sw` and `cal` use, so their numbers do not depend on the window.
void fullSweep() {
    sweepStartMs = millis();
    startSweep(IrSweep::Window());
    while (!stepSweep()) {
    }
}

// Averages kCalSweeps clean sweeps into a calibration point at `cm`.
void calibrate(float cm) {
    float sum = 0;
    float sumSquares = 0;
    int clean = 0;
    for (int i = 0; i < kCalAttempts && clean < kCalSweeps; ++i) {
        fullSweep();
        if (lastResult.state == IrSweep::State::Ok) {
            sum += lastResult.marginDb;
            sumSquares += lastResult.marginDb * lastResult.marginDb;
            ++clean;
        }
    }
    if (clean < kCalSweeps) {
        Serial.printf("ERR cal: %d of %d sweeps clean, last state=%s\n", clean, kCalAttempts,
                      stateName(lastResult.state));
        return;
    }
    const float db = sum / clean;
    const float variance = sumSquares / clean - db * db;
    if (!calibration.add(cm, db)) {
        Serial.printf("ERR cal: cannot add %.1f cm\n", cm);
        return;
    }
    saveCalibration();
    Serial.printf("CAL cm=%.1f db=%.1f sd=%.1f points=%d\n", cm, db,
                  variance > 0 ? sqrtf(variance) : 0.0f, calibration.count());
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
    } else if (strncmp(line, "cal ", 4) == 0) {
        calibrate(static_cast<float>(atof(line + 4)));
    } else if (strncmp(line, "demo ", 5) == 0) {
        // A made-up sweep with the given margin, for screenshots: the
        // measuring pauses so it stays on screen; KEY1 resumes it.
        const float db = static_cast<float>(atof(line + 5));
        paused = true;
        demo = true;
        lastBursts = bursts;
        for (int i = 0; i < IrSweep::kLevels; ++i) {
            lastHits[i] = -IrSweep::levelDb(i) <= db ? bursts : 0;
        }
        lastNoise = 0;
        lastResult = IrSweep::estimate(lastHits, lastBursts);
        lastResult.marginDb = db;
        lastRange = calibration.estimate(db);
        measured = true;
        Serial.printf("DEMO db=%.1f ", db);
        printRange();
        Serial.print('\n');
    } else if (strncmp(line, "calset ", 7) == 0) {
        // A point measured earlier: "calset <cm> <db>".
        float cm = 0;
        float db = 0;
        if (sscanf(line + 7, "%f %f", &cm, &db) == 2 && calibration.add(cm, db)) {
            saveCalibration();
            Serial.printf("CAL cm=%.1f db=%.1f sd=0.0 points=%d\n", cm, db, calibration.count());
        } else {
            Serial.printf("ERR calset '%s'\n", line + 7);
        }
    } else if (strcmp(line, "callist") == 0) {
        printCalibration();
    } else if (strcmp(line, "calclr") == 0) {
        calibration.clear();
        saveCalibration();
        printCalibration();
    } else if (strncmp(line, "freq ", 5) == 0) {
        const uint32_t actual = ir.setCarrier(static_cast<uint32_t>(atol(line + 5)));
        startSweep(IrSweep::Window());
        Serial.printf("FREQ %u\n", static_cast<unsigned>(actual));
    } else if (strncmp(line, "gap ", 4) == 0) {
        // "gap <after heard> [<after not heard>]", in microseconds
        unsigned long heard = 0;
        unsigned long missed = 0;
        const int given = sscanf(line + 4, "%lu %lu", &heard, &missed);
        if (given >= 1) {
            ir.setGap(heard, given == 2 ? missed : ir.missGap());
        }
        startSweep(IrSweep::Window());
        Serial.printf("GAP %u %u\n", static_cast<unsigned>(ir.gap()),
                      static_cast<unsigned>(ir.missGap()));
    } else if (strncmp(line, "wave ", 5) == 0) {
        // 4 ms in 20 us steps; the burst is the first 0.8 ms.
        static char samples[201];
        ir.wave(IrSweep::duty(atoi(line + 5)), 20, samples, 200);
        samples[200] = '\0';
        Serial.printf("WAVE %s\n", samples);
    } else if (strncmp(line, "n ", 2) == 0) {
        const int n = atoi(line + 2);
        bursts = static_cast<uint8_t>(n < 1 ? 1 : n > 32 ? 32 : n);
        startSweep(IrSweep::Window());
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
    loadCalibration();
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
            paused = !paused && !demo;
            demo = false;
            startSweep(IrSweep::Window());
            sweepStartMs = now;
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
        screen.calibrated = calibration.count() >= 2;
        screen.cm10 = lastRange.valid ? static_cast<int>(lastRange.cm * 10 + 0.5f) : -1;
        screen.extrapolated = lastRange.extrapolated;
        memcpy(screen.hits, lastHits, sizeof(screen.hits));
        screen.bursts = lastBursts;
        screen.measured = measured;
        screen.paused = paused && !demo;
        screen.battery = batteryShown;
        renderer.draw(screen);
    }

    if (haveAccel) {
        printAccel(now, ax, ay, az);
    }

    // A power level takes about 65 ms, which paces the loop by itself.
    delay(measuring ? 1 : kLoopMs);
}

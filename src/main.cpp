#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "BatteryFilter.h"
#include "DisplayTimeout.h"
#include "Orientation.h"
#include "PickupDetector.h"
#include "Renderer.h"

namespace {

constexpr uint8_t kBrightness = 120;
constexpr uint32_t kLoopMs = 20;           // 50 Hz
constexpr uint32_t kBatteryPeriodMs = 5000; // the level changes slowly

Renderer renderer;
DisplayTimeout displayTimeout;
Orientation orientation(Orientation::Layout::Portrait);
PickupDetector pickupDetector;
BatteryFilter battery;

bool displayAwake = true;
uint32_t countFromMs = 0; // KEY1 restarts the count
int batteryShown = -1;
uint32_t batteryReadMs = 0;

// Self-testing over serial: line commands, see CLAUDE.md.
char command[32];
size_t commandLength = 0;
uint8_t pendingKeys = 0; // presses from k1 / k2: bit 0 KEY1, bit 1 KEY2
bool accelOn = false;
uint32_t accelPrintedMs = 0;

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
    M5.begin(cfg);
    // M5Unified leaves Serial alone (its serial_baudrate is 0 by default).
    Serial.begin(115200);

    M5.Display.setBrightness(kBrightness);
    if (!renderer.begin()) {
        Serial.println("ERR no memory for the frame");
    }

    const uint32_t now = millis();
    displayTimeout.begin(now);
    countFromMs = now;
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
            countFromMs = now;
        }
        if (haveAccel) {
            renderer.setFlipped(orientation.update(now, ax, ay, az));
        }
        readBattery(now, false);
        renderer.draw({(now - countFromMs) / 1000, batteryShown});
    }

    if (haveAccel) {
        printAccel(now, ax, ay, az);
    }

    delay(kLoopMs);
}

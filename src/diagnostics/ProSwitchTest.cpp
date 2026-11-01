#if defined(GADGET_OS_PRO_SWITCH_TEST) && GADGET_OS_PRO_SWITCH_TEST

#include <Arduino.h>

namespace {
// Original N16R2 prototype only. These pins may drive the LCD on later revisions.
struct Contact {
  uint8_t pin;
  int raw = HIGH;
  int stable = HIGH;
  uint32_t changedAt = 0;
};

Contact contacts[] = {{1}, {4}, {43}};
constexpr uint32_t DebounceMs = 25;
uint32_t lastStatus = 0;
bool wasConnected = false;

void printStatus()
{
  Serial.print("STATE:");
  for (const auto &contact : contacts) {
    Serial.printf(" GPIO%u=%s", contact.pin,
                  contact.stable == LOW ? "LOW" : "HIGH");
  }
  Serial.println();
}

void printHelp()
{
  Serial.println("\nPro switch test: ORIGINAL N16R2 prototype, GPIO1 / GPIO4 / GPIO43");
  Serial.println("Inputs use weak pull-ups; external wiring determines the resting level.");
  Serial.println("Original prototype observations: idle LOW, operated HIGH.");
  Serial.println("Release the switch, then PULL DOWN and release, PUSH UP and release,");
  Serial.println("then PRESS IN and release. Note the GPIO printed for each action.");
  Serial.println("Changes are debounced for 25 ms; state repeats every 2 seconds.");
  printStatus();
}
} // namespace

void setup()
{
  for (auto &contact : contacts) {
    pinMode(contact.pin, INPUT_PULLUP);
    contact.raw = contact.stable = digitalRead(contact.pin);
    contact.changedAt = millis();
  }
  Serial.begin(115200);
}

void loop()
{
  const uint32_t now = millis();
  const bool connected = static_cast<bool>(Serial);
  if (connected && !wasConnected) printHelp();
  wasConnected = connected;

  for (auto &contact : contacts) {
    const int value = digitalRead(contact.pin);
    if (value != contact.raw) {
      contact.raw = value;
      contact.changedAt = now;
    }
    if (contact.raw != contact.stable && now - contact.changedAt >= DebounceMs) {
      contact.stable = contact.raw;
      if (connected) {
        Serial.printf("%lu ms: GPIO%u %s\n", static_cast<unsigned long>(now),
                      contact.pin, contact.stable == LOW ? "LOW" : "HIGH");
      }
    }
  }
  if (connected && now - lastStatus >= 2000) {
    printStatus();
    lastStatus = now;
  }
  delay(5);
}

#endif

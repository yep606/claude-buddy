// Claude Buddy for the LilyGO T-Display-S3.
// Shows what Claude Code is doing and answers its permission prompts and
// questions with the two buttons. Talks to bridge.py on the PC over USB.
// Without the PC, press the top button on the "Waiting for PC" screen for a demo.
//
// Board settings: see the README (ESP32S3 Dev Module, USB CDC On Boot: Enabled,
// PSRAM: OPI PSRAM, Flash 16MB, Partition 3MB APP/9.9MB FATFS), esp32 core 2.0.14.
// First start shows a Setup screen: press the button nearest the top-right corner.
// Hold both buttons 3 s at any time (outside a prompt) to run Setup again.

#include "protocol.h"

void setup() {
  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, HIGH);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_KEY, INPUT_PULLUP);

  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);  // never stall the animation when nobody is listening
#endif

  prefs.begin("buddy", false);
  rotation = prefs.getUChar("rot", 1);
  uint8_t top = prefs.getUChar("top", 255);
  applyButtonMap(top);
  if (top == 255) startCalibration();

  tft.init();
  tft.setRotation(rotation);
  tft.fillScreen(C_BG);
  spr.setColorDepth(16);
  if (!spr.createSprite(320, 170)) {
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Sprite failed: set PSRAM to OPI PSRAM", 10, 10, 2);
    analogWrite(PIN_BACKLIGHT, BL_NORMAL);
    while (true) delay(1000);
  }
  sendLine("HELLO\tv=1");
}

void loop() {
  static uint32_t lastFrame = 0, lastTick = 0;
  uint32_t now = millis();

  readSerial();
  pollButton(btnBoot, now);
  pollButton(btnKey, now);
  uint32_t dt = now - lastTick;
  lastTick = now;
  if (calibrating) updateCalibration(now);
  else updateHolds(now, dt);
  updateDemo(now);

  if (linkUp && since(lastRx) > LINK_TIMEOUT_MS) dropLink();
  if (prompt.active && prompt.ask && !demo.active && since(prompt.at) > (prompt.tmo + 5) * 1000UL) {
    prompt.active = false;  // the PC should have cleared it by now
  }

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    render(now);
  }
  delay(1);
}

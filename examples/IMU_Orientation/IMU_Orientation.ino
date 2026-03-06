/**
 * IMU_Orientation — Gyroscope orientation tracking example
 *
 * Demonstrates:
 *  - Real-time yaw, pitch, roll from the QMI8658 6-axis IMU
 *  - Accelerometer and gyroscope raw readings
 *  - Orientation reset on tap
 *
 * No LVGL required.
 */

#include "WS_ESP32_Touch28.h"

// ── Reset orientation on tap ────────────────────────────────────────
static void onTap(TouchEvent& evt) {
  if (evt.gesture == GESTURE_CLICK) {
    IMU_ResetOrientation();
    Serial.println("Orientation reset to zero!");
  }
}

void setup() {
  Serial.begin(115200);
  WS_ESP32_Init();

  LCD_FB_Start();
  LCD_FB_Clear(LCD_Color565(0, 0x3a, 0x57));
  LCD_FB_End();

  // Full-screen tap to reset orientation
  TouchZone tapZone = Touch_MakeRect(
    1, 0, 0, LCD_WIDTH, LCD_HEIGHT,
    GESTURE_CLICK, onTap);
  Touch_Gesture_AddZone(&tapZone);

  Serial.println("IMU_Orientation ready!");
  Serial.println("Tap screen to reset orientation to zero.");
  Serial.println();
}

void loop() {
  static uint32_t lastPrint = 0;

  if (millis() - lastPrint > 100) {   // 10 Hz serial output
    lastPrint = millis();

    Serial.printf("Yaw: %7.1f  Pitch: %7.1f  Roll: %7.1f  |  "
                  "Acc(%.2f, %.2f, %.2f)  Gyro(%.1f, %.1f, %.1f)\n",
                  IMU_GetYaw(), IMU_GetPitch(), IMU_GetRoll(),
                  Accel.x, Accel.y, Accel.z,
                  Gyro.x, Gyro.y, Gyro.z);
  }

  WS_ESP32_Loop();
  delay(10);
}

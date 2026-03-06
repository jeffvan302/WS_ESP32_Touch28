/**
 * Basic_NoLVGL — Minimal example WITHOUT LVGL
 *
 * Demonstrates:
 *  - Display initialization and framebuffer drawing
 *  - Touch gesture zones with callbacks
 *  - Battery voltage reading
 *  - SD card image loading
 *
 * No LVGL library required.
 */

#include "WS_ESP32_Touch28.h"

// ── Touch callback ──────────────────────────────────────────────────
static void onScreenTap(TouchEvent& evt) {
  if (evt.gesture == GESTURE_CLICK) {
    Serial.printf("Tap at (%d, %d)\n", evt.x, evt.y);

    // Draw a small square where the user tapped
    LCD_FB_Start();
    LCD_FB_FillRect(evt.x - 5, evt.y - 5, 10, 10, LCD_Color565(255, 0, 0));
    LCD_FB_End();
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize all hardware (display, touch, SD, audio, IMU, battery, power)
  WS_ESP32_Init();

  // Draw a blue background with white rectangle
  LCD_FB_Start();
  LCD_FB_Clear(LCD_Color565(0, 0x3a, 0x57));
  LCD_FB_FillRect(20, 20, 200, 60, LCD_Color565(255, 255, 255));
  LCD_FB_End();

  // Register a full-screen touch zone for tap detection
  TouchZone fullScreen = Touch_MakeRect(
    1,                    // zone ID
    0, 0,                 // top-left corner
    LCD_WIDTH, LCD_HEIGHT,// full screen
    GESTURE_CLICK,        // detect single taps
    onScreenTap           // callback function
  );
  Touch_Gesture_AddZone(&fullScreen);

  // Load an image from SD card (if available)
  if (SD_IsMounted()) {
    if (SD_Exists("/images/logo.jpg")) {
      LCD_FB_Start();
      SD_LoadImageFit("/images/logo.jpg",
                      lcd_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                      120, 200,        // centered position
                      200, 150,        // max width/height
                      IMG_ROTATE_0);
      LCD_FB_End();
    }
  }

  Serial.println("Basic_NoLVGL ready!");
}

void loop() {
  // Print battery voltage every ~2 seconds
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    Serial.printf("Battery: %.2f V\n", BAT_Get_Volts());
  }

  // Process touch, power key, IMU, and LCD update
  WS_ESP32_Loop();
  delay(10);
}

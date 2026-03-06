/**
 * Touch_Gestures_Demo — Gesture recognition example
 *
 * Demonstrates:
 *  - Rectangular and circular touch zones
 *  - Click, double-click, swipe, long-press, and drag gestures
 *  - Zone enable/disable at runtime
 *
 * No LVGL required.
 */

#include "WS_ESP32_Touch28.h"

// Zone IDs
#define ZONE_TOP_BUTTON    1
#define ZONE_BOTTOM_BUTTON 2
#define ZONE_SWIPE_AREA    3

static uint16_t bgColor = 0x0000;

// ── Callbacks ───────────────────────────────────────────────────────
static void onTopButton(TouchEvent& evt) {
  if (evt.gesture == GESTURE_CLICK) {
    Serial.println("Top button: CLICK");
    bgColor = LCD_Color565(0, 100, 0);   // green
  } else if (evt.gesture == GESTURE_LONG_PRESS) {
    Serial.println("Top button: LONG PRESS");
    bgColor = LCD_Color565(100, 0, 0);   // red
  }

  LCD_FB_Start();
  LCD_FB_Clear(bgColor);
  drawUI();
  LCD_FB_End();
}

static void onBottomButton(TouchEvent& evt) {
  if (evt.gesture == GESTURE_CLICK) {
    Serial.println("Bottom button: CLICK");
    bgColor = LCD_Color565(0, 0, 100);   // blue
  } else if (evt.gesture == GESTURE_DOUBLE_CLICK) {
    Serial.println("Bottom button: DOUBLE CLICK — resetting");
    bgColor = 0x0000;                    // black
  }

  LCD_FB_Start();
  LCD_FB_Clear(bgColor);
  drawUI();
  LCD_FB_End();
}

static void onSwipeArea(TouchEvent& evt) {
  if (evt.gesture == GESTURE_SWIPE_LEFT) {
    Serial.println("Swipe LEFT");
  } else if (evt.gesture == GESTURE_SWIPE_RIGHT) {
    Serial.println("Swipe RIGHT");
  } else if (evt.gesture == GESTURE_SWIPE_UP) {
    Serial.println("Swipe UP");
  } else if (evt.gesture == GESTURE_SWIPE_DOWN) {
    Serial.println("Swipe DOWN");
  } else if (evt.gesture == GESTURE_DRAG) {
    // Draw a trail while dragging
    LCD_FB_Start();
    LCD_FB_FillRect(evt.x - 3, evt.y - 3, 6, 6, LCD_Color565(255, 255, 0));
    LCD_FB_End();
  }
}

// ── Draw UI helper ──────────────────────────────────────────────────
void drawUI() {
  // Top button area (white rectangle)
  LCD_FB_FillRect(60, 20, 120, 50, LCD_Color565(255, 255, 255));
  // Bottom button area (gray rectangle)
  LCD_FB_FillRect(60, 250, 120, 50, LCD_Color565(128, 128, 128));
  // Swipe area outline
  LCD_FB_DrawRect(10, 90, 220, 140, LCD_Color565(0, 255, 255));
}

void setup() {
  Serial.begin(115200);
  WS_ESP32_Init();

  LCD_FB_Start();
  LCD_FB_Clear(bgColor);
  drawUI();
  LCD_FB_End();

  // Top button: click + long press
  TouchZone top = Touch_MakeRect(ZONE_TOP_BUTTON,
    60, 20, 120, 50,
    GESTURE_CLICK | GESTURE_LONG_PRESS,
    onTopButton);
  Touch_Gesture_AddZone(&top);

  // Bottom button: click + double click
  TouchZone bottom = Touch_MakeRect(ZONE_BOTTOM_BUTTON,
    60, 250, 120, 50,
    GESTURE_CLICK | GESTURE_DOUBLE_CLICK,
    onBottomButton);
  Touch_Gesture_AddZone(&bottom);

  // Middle swipe area: all swipes + drag
  TouchZone swipe = Touch_MakeRect(ZONE_SWIPE_AREA,
    10, 90, 220, 140,
    GESTURE_SWIPE_ANY | GESTURE_DRAG,
    onSwipeArea);
  Touch_Gesture_AddZone(&swipe);

  Serial.println("Touch_Gestures_Demo ready!");
  Serial.println("Top box: tap=green, long-press=red");
  Serial.println("Bottom box: tap=blue, double-tap=reset");
  Serial.println("Middle box: swipe any direction, or drag to draw");
}

void loop() {
  WS_ESP32_Loop();
  delay(10);
}

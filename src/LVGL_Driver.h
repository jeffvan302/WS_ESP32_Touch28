#pragma once

// ═══════════════════════════════════════════════════════════════════
//  LVGL Driver — optional layer over Display_ST7789 + Touch_CST328
// ═══════════════════════════════════════════════════════════════════
//
//  If the LVGL library is installed, this module provides:
//    - Display flush callback  (renders into the existing PSRAM framebuffer)
//    - Touch input callback    (reads from the CST328 via Touch_Gestures API)
//    - Tick source             (millis()-based)
//    - Init / Loop functions
//
//  If LVGL is NOT installed, everything compiles to empty stubs so the
//  rest of the project builds without changes.
//
//  Usage:
//    #include "LVGL_Driver.h"
//
//    void setup() {
//      LCD_Init();
//      #if HAS_LVGL
//        LVGL_Init();
//      #endif
//    }
//    void loop() {
//      #if HAS_LVGL
//        LVGL_Loop();
//      #endif
//    }
// ═══════════════════════════════════════════════════════════════════

// ── Detect LVGL at compile time ────────────────────────────────────
#if __has_include(<lvgl.h>)
  #define HAS_LVGL 1
  #include <lvgl.h>
#else
  #define HAS_LVGL 0
#endif

#include "Display_ST7789.h"
#include "Touch_Gestures.h"

// ── Draw buffer sizing ─────────────────────────────────────────────
// Each buffer holds LVGL_BUF_ROWS rows of the display.
// Uses LCD_PANEL_H (320) as the row width — this is the maximum
// possible lcd_width (landscape).  Buffers are allocated once and
// work for any rotation without reallocation.
#define LVGL_BUF_ROWS      32
#define LVGL_BUF_BYTES      ((uint32_t)LCD_PANEL_H * LVGL_BUF_ROWS * sizeof(uint16_t))

// ═══════════════════════════════════════════════════════════════════
//  PUBLIC API
// ═══════════════════════════════════════════════════════════════════

#if HAS_LVGL

// Initialize LVGL, register the display and touch drivers.
// Call AFTER LCD_Init() and Touch_Init() (which is called inside LCD_Init).
void LVGL_Init(void);

// Run the LVGL timer handler.  Call from the main loop().
// Internally rate-limits itself so calling it every iteration is fine.
void LVGL_Loop(void);

// Update the LVGL display resolution after LCD_SetRotation().
// Called automatically by LCD_SetRotation() — you don't normally
// need to call this directly.
void LVGL_SetRotation(uint8_t rotation);

// Access the LVGL display and input device objects for advanced use.
lv_display_t* LVGL_GetDisplay(void);
lv_indev_t*   LVGL_GetIndev(void);

#else  // LVGL not installed — provide empty stubs

static inline void LVGL_Init(void)  {}
static inline void LVGL_Loop(void)  {}
static inline void LVGL_SetRotation(uint8_t) {}

#endif // HAS_LVGL

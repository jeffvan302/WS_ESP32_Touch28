#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Touch_CST328.h"

// ── Fixed panel dimensions (physical LCD) ──────────────────────────
#define LCD_PANEL_W   240   // Native panel width  (portrait)
#define LCD_PANEL_H   320   // Native panel height (portrait)

// ── Runtime display dimensions (change with rotation) ──────────────
// Use LCD_WIDTH / LCD_HEIGHT throughout — they resolve to the current
// rotation's effective dimensions.
extern uint16_t lcd_width;
extern uint16_t lcd_height;
extern uint8_t  lcd_rotation;    // 0–3

#define LCD_WIDTH   lcd_width
#define LCD_HEIGHT  lcd_height

#define SPIFreq                        80000000
#define EXAMPLE_PIN_NUM_MISO           -1
#define EXAMPLE_PIN_NUM_MOSI           45
#define EXAMPLE_PIN_NUM_SCLK           40
#define EXAMPLE_PIN_NUM_LCD_CS         42
#define EXAMPLE_PIN_NUM_LCD_DC         41
#define EXAMPLE_PIN_NUM_LCD_RST        39

#define LCD_Backlight_PIN   5
#define PWM_Channel     1       // PWM Channel   
#define Frequency       20000   // PWM frequencyconst        
#define Resolution      10       // PWM resolution ratio     MAX:13
#define Dutyfactor      500     // PWM Dutyfactor      
#define Backlight_MAX   100      

#define VERTICAL   0
#define HORIZONTAL 1

#define Offset_X 0
#define Offset_Y 0

// Total bytes for a full 16-bit framebuffer: 240 * 320 * 2 = 153,600
// Uses fixed panel dimensions — total pixel count is the same regardless of rotation.
#define LCD_FB_SIZE  ((uint32_t)LCD_PANEL_W * LCD_PANEL_H * sizeof(uint16_t))

// ── Dirty rectangle tracking ──────────────────────────────────────
// Tracks the bounding box of all draw operations since the last
// LCD_FB_Start().  LCD_Update() only streams this region via SPI.
struct DirtyRect {
  int16_t x1, y1, x2, y2;   // inclusive pixel bounds
  bool    dirty;             // true if any draw op occurred
};

extern DirtyRect lcd_dirty;

// ── Framebuffer update flag ─────────────────────────────────────────
// Protected by lcd_fb_mutex.  Set via LCD_FB_End(); cleared by LCD_Update().
extern volatile bool update_lcd_;

// Pointer to the PSRAM-backed framebuffer (allocated in LCD_Init)
extern uint16_t* lcd_framebuffer;

// FreeRTOS mutex that guards the framebuffer for dual-core safety.
// Taken by LCD_FB_Start / LCD_Update, released by LCD_FB_End / LCD_Update.
extern SemaphoreHandle_t lcd_fb_mutex;

extern uint8_t LCD_Backlight;

// ── Core LCD functions ──────────────────────────────────────────────
void LCD_Init(void);

// ── Rotation ──────────────────────────────────────────────────────
//  0 = portrait  (240×320)   MADCTL 0x00
//  1 = landscape (320×240)   MADCTL 0x60
//  2 = portrait  inverted    MADCTL 0xC0
//  3 = landscape inverted    MADCTL 0xA0
//
// Updates LCD_WIDTH / LCD_HEIGHT, remaps touch coordinates,
// clears the framebuffer, and (if LVGL is active) updates the
// LVGL display resolution.  Can be called at any time after LCD_Init().
void LCD_SetRotation(uint8_t rotation);
uint8_t LCD_GetRotation(void);

void Backlight_Init(void);
void Set_Backlight(uint8_t Light);

// ── Dual-core framebuffer access ────────────────────────────────────
//
//  Usage from any core:
//
//      LCD_FB_Start();              // blocks until SPI stream finishes
//      LCD_FB_Clear(bg);            // safe to write to framebuffer
//      LCD_FB_FillRect(...);
//      LCD_FB_End();                // marks frame ready, releases lock
//
//  Meanwhile LCD_Update() (call it from a loop or a FreeRTOS task on
//  either core) will stream the buffer to the ST7789 when a new frame
//  is ready, holding the mutex for the duration of the SPI transfer.

// Block until the framebuffer is safe to write (SPI transfer complete).
void LCD_FB_Start(void);

// Mark the framebuffer as updated and release the lock so LCD_Update()
// can stream it out.
void LCD_FB_End(void);

// ── Framebuffer -> LCD streaming ────────────────────────────────────
// Call this in a loop or FreeRTOS task.  When update_lcd_ is true it
// acquires the mutex, streams the PSRAM framebuffer to the ST7789,
// clears the flag, and releases the mutex.
void LCD_Update(void);

// ── Drawing helpers (all write to the PSRAM framebuffer) ────────────
//    *** Call these ONLY between LCD_FB_Start() and LCD_FB_End() ***
//    All draw functions automatically expand the dirty rectangle.

// Manually mark a region as dirty (for code that writes lcd_framebuffer directly)
void LCD_FB_MarkDirty(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

// Force the entire screen to be sent on next update
void LCD_FB_MarkFullDirty(void);

// Convert 8-bit RGB to RGB565
inline uint16_t LCD_Color565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Fill the entire framebuffer with a single colour
void LCD_FB_Clear(uint16_t color);

// Draw a single pixel
void LCD_FB_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

// Fill a rectangle  (x,y = top-left corner, w/h = dimensions in pixels)
void LCD_FB_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// Draw a horizontal line
void LCD_FB_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

// Draw a vertical line
void LCD_FB_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

// Draw a 1-pixel rectangle outline
void LCD_FB_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// Blit an arbitrary RGB565 buffer into the framebuffer at (dx, dy)
void LCD_FB_Blit(uint16_t dx, uint16_t dy, uint16_t w, uint16_t h, const uint16_t* src);

// ── Legacy test (still works, bypasses framebuffer) ─────────────────
void LCD_TestSolidBackgroundStep_Streamed(void);

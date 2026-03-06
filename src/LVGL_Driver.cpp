#include "LVGL_Driver.h"

#if HAS_LVGL

#include <esp_heap_caps.h>

// ── Module state ───────────────────────────────────────────────────
static lv_display_t* lvgl_display = nullptr;
static lv_indev_t*   lvgl_indev  = nullptr;
static uint16_t*     lvgl_buf1   = nullptr;
static uint16_t*     lvgl_buf2   = nullptr;

// ═══════════════════════════════════════════════════════════════════
//  TICK SOURCE
// ═══════════════════════════════════════════════════════════════════
//  LVGL v9 accepts a callback that returns the current time in ms.
//  This avoids the need for a dedicated timer or ISR.

static uint32_t lvgl_tick_cb(void)
{
  return (uint32_t)millis();
}

// ═══════════════════════════════════════════════════════════════════
//  DISPLAY FLUSH CALLBACK
// ═══════════════════════════════════════════════════════════════════
//  Called by LVGL when a region of the screen has been rendered into
//  one of the draw buffers.  We copy the pixels into the existing
//  PSRAM framebuffer, mark the dirty region, signal LCD_Update(),
//  and tell LVGL we're done.

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
  uint16_t* src = (uint16_t*)px_map;

  int16_t x1 = area->x1;
  int16_t y1 = area->y1;
  int16_t x2 = area->x2;
  int16_t y2 = area->y2;

  uint16_t w = (uint16_t)(x2 - x1 + 1);

  // Take the framebuffer mutex so we don't collide with LCD_Update()
  if (lcd_fb_mutex) {
    xSemaphoreTake(lcd_fb_mutex, portMAX_DELAY);
  }

  // Copy each row of the rendered area into the PSRAM framebuffer
  for (int16_t y = y1; y <= y2; y++) {
    if (y >= 0 && y < LCD_HEIGHT) {
      uint32_t fb_offset = (uint32_t)y * LCD_WIDTH + x1;
      memcpy(&lcd_framebuffer[fb_offset], src, w * sizeof(uint16_t));
    }
    src += w;
  }

  // Mark the region dirty so LCD_Update() streams it via SPI
  LCD_FB_MarkDirty((uint16_t)x1, (uint16_t)y1, w, (uint16_t)(y2 - y1 + 1));

  // Signal that a new frame region is ready
  update_lcd_ = true;

  if (lcd_fb_mutex) {
    xSemaphoreGive(lcd_fb_mutex);
  }

  // Tell LVGL we're done flushing
  lv_display_flush_ready(disp);
}

// ═══════════════════════════════════════════════════════════════════
//  TOUCH INPUT CALLBACK
// ═══════════════════════════════════════════════════════════════════
//  Called by LVGL to poll the touch state.  Uses the existing
//  Touch_Gestures API which already handles CST328 interrupt-driven
//  reads and data buffering.

static void lvgl_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
  int16_t tx, ty;
  if (Touch_IsPressed() && Touch_GetRawXY(&tx, &ty)) {
    data->point.x = tx;
    data->point.y = ty;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ═══════════════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════════════

void LVGL_Init(void)
{
  printf("Starting LVGL!\n");
  // ── Core LVGL init ────────────────────────────────────────────
  lv_init();

  // ── Tick source ───────────────────────────────────────────────
  lv_tick_set_cb(lvgl_tick_cb);

  // ── Display driver ────────────────────────────────────────────
  lvgl_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  if (!lvgl_display) {
    Serial.println("[LVGL] Display creation failed!");
    return;
  }

  // Allocate double draw buffers in PSRAM
  lvgl_buf1 = (uint16_t*)heap_caps_malloc(LVGL_BUF_BYTES, MALLOC_CAP_SPIRAM);
  lvgl_buf2 = (uint16_t*)heap_caps_malloc(LVGL_BUF_BYTES, MALLOC_CAP_SPIRAM);

  if (!lvgl_buf1 || !lvgl_buf2) {
    Serial.println("[LVGL] PSRAM buffer allocation failed!");
    // Fall back to single buffer in internal RAM
    if (lvgl_buf1) { heap_caps_free(lvgl_buf1); lvgl_buf1 = nullptr; }
    if (lvgl_buf2) { heap_caps_free(lvgl_buf2); lvgl_buf2 = nullptr; }

    lvgl_buf1 = (uint16_t*)malloc(LVGL_BUF_BYTES);
    if (!lvgl_buf1) {
      Serial.println("[LVGL] Buffer allocation failed completely!");
      return;
    }
    lv_display_set_buffers(lvgl_display,
                           lvgl_buf1, nullptr,
                           LVGL_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    Serial.println("[LVGL] Using single internal RAM buffer");
  } else {
    lv_display_set_buffers(lvgl_display,
                           lvgl_buf1, lvgl_buf2,
                           LVGL_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    Serial.printf("[LVGL] Double PSRAM buffers: %u bytes each\n",
                  (unsigned)LVGL_BUF_BYTES);
  }

  lv_display_set_flush_cb(lvgl_display, lvgl_flush_cb);

  // ── Touch input driver ────────────────────────────────────────
  lvgl_indev = lv_indev_create();
  if (lvgl_indev) {
    lv_indev_set_type(lvgl_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_indev, lvgl_touch_read_cb);
    Serial.println("[LVGL] Touch input registered");
  }

  Serial.println("[LVGL] Initialized OK");
}

// ═══════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════
//  Call from the main Arduino loop().  LVGL's timer handler drives
//  rendering, animations, and input processing.

void LVGL_Loop(void)
{
  lv_timer_handler();
}

// ═══════════════════════════════════════════════════════════════════
//  ROTATION UPDATE
// ═══════════════════════════════════════════════════════════════════
//  Called by LCD_SetRotation() after the hardware MADCTL and
//  lcd_width / lcd_height have already been updated.  We just need
//  to tell LVGL about the new resolution.

void LVGL_SetRotation(uint8_t rotation)
{
  (void)rotation;   // we read lcd_width/lcd_height directly
  if (!lvgl_display) return;

  lv_display_set_resolution(lvgl_display, lcd_width, lcd_height);
  Serial.printf("[LVGL] Resolution updated to %dx%d\n", lcd_width, lcd_height);
}

// ═══════════════════════════════════════════════════════════════════
//  ACCESSORS
// ═══════════════════════════════════════════════════════════════════

lv_display_t* LVGL_GetDisplay(void) { return lvgl_display; }
lv_indev_t*   LVGL_GetIndev(void)   { return lvgl_indev; }

#endif // HAS_LVGL

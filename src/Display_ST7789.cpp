#include "Display_ST7789.h"
#include "LVGL_Driver.h"      // for LVGL_SetRotation() (compiles to no-op if LVGL absent)
#include <esp_heap_caps.h>
#include <string.h>   // memset / memcpy

// ── Globals ─────────────────────────────────────────────────────────
volatile bool update_lcd_    = false;
uint16_t*     lcd_framebuffer = nullptr;

// Runtime display dimensions (default: portrait)
uint16_t lcd_width    = LCD_PANEL_W;
uint16_t lcd_height   = LCD_PANEL_H;
uint8_t  lcd_rotation = 0;

// FreeRTOS mutex for dual-core framebuffer protection
SemaphoreHandle_t lcd_fb_mutex = nullptr;

// Dirty rectangle — tracks bounding box of all draw ops this frame
DirtyRect lcd_dirty = { 0, 0, 0, 0, false };

// ── Internal: expand the dirty rectangle to include a region ────────
static inline void dirty_expand(int16_t x, int16_t y, int16_t w, int16_t h)
{
  int16_t nx2 = x + w - 1;
  int16_t ny2 = y + h - 1;

  // Clamp to screen bounds
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (nx2 >= LCD_WIDTH)  nx2 = LCD_WIDTH  - 1;
  if (ny2 >= LCD_HEIGHT) ny2 = LCD_HEIGHT - 1;
  if (x > nx2 || y > ny2) return;  // completely off-screen

  if (!lcd_dirty.dirty) {
    lcd_dirty.x1 = x;
    lcd_dirty.y1 = y;
    lcd_dirty.x2 = nx2;
    lcd_dirty.y2 = ny2;
    lcd_dirty.dirty = true;
  } else {
    if (x   < lcd_dirty.x1) lcd_dirty.x1 = x;
    if (y   < lcd_dirty.y1) lcd_dirty.y1 = y;
    if (nx2 > lcd_dirty.x2) lcd_dirty.x2 = nx2;
    if (ny2 > lcd_dirty.y2) lcd_dirty.y2 = ny2;
  }
}

// ── SPI ─────────────────────────────────────────────────────────────
SPIClass LCDspi(FSPI);

void SPI_Init()
{
  LCDspi.begin(EXAMPLE_PIN_NUM_SCLK, EXAMPLE_PIN_NUM_MISO, EXAMPLE_PIN_NUM_MOSI);
}

// ── Low-level helpers (kept from original) ──────────────────────────
static inline void LCD_BeginWrite()
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
}

static inline void LCD_EndWrite()
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}

static inline void LCD_WriteCmd(uint8_t cmd)
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW);
  LCDspi.transfer(cmd);
}

static inline void LCD_WriteData8(uint8_t data)
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  LCDspi.transfer(data);
}

static inline void LCD_SetAddrWindow_NoCS(uint16_t x1, uint16_t y1,
                                          uint16_t x2, uint16_t y2)
{
  // CASET
  LCD_WriteCmd(0x2A);
  LCD_WriteData8(x1 >> 8); LCD_WriteData8(x1 & 0xFF);
  LCD_WriteData8(x2 >> 8); LCD_WriteData8(x2 & 0xFF);

  // RASET
  LCD_WriteCmd(0x2B);
  LCD_WriteData8(y1 >> 8); LCD_WriteData8(y1 & 0xFF);
  LCD_WriteData8(y2 >> 8); LCD_WriteData8(y2 & 0xFF);

  // RAMWR
  LCD_WriteCmd(0x2C);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH); // ready for pixel stream
}

void LCD_WriteCommand(uint8_t Cmd)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW);
  LCDspi.transfer(Cmd);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}

void LCD_WriteData(uint8_t Data)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  LCDspi.transfer(Data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}

void LCD_WriteData_Word(uint16_t Data)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  LCDspi.transfer16(Data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}

void LCD_WriteData_nbyte(uint8_t* SetData, uint8_t* ReadData, uint32_t Size)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  LCDspi.transferBytes(SetData, ReadData, Size);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}

// ── Reset & Init ────────────────────────────────────────────────────
void LCD_Reset(void)
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, LOW);
  delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH);
  delay(50);
}

void LCD_Init(void)
{
  pinMode(EXAMPLE_PIN_NUM_LCD_CS, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_DC, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_RST, OUTPUT);
  SPI_Init();

  LCD_Reset();
  //************* Start Initial Sequence **********//

  delay(120);
  LCD_WriteCommand(0x29);    //Display on
  delay(120);
  LCD_WriteCommand(0x11);
  delay(120);

  // Set default rotation (portrait) — sends MADCTL + updates lcd_width/lcd_height
  LCD_SetRotation(0);

  LCD_WriteCommand(0x3A);
  LCD_WriteData(0x05);

  LCD_WriteCommand(0xB0);
  LCD_WriteData(0x00);
  LCD_WriteData(0xE8);

  LCD_WriteCommand(0xB2);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x00);
  LCD_WriteData(0x33);
  LCD_WriteData(0x33);

  LCD_WriteCommand(0xB7);
  LCD_WriteData(0x75);   //VGH=14.97V,VGL=-7.67V

  LCD_WriteCommand(0xBB);
  LCD_WriteData(0x1A);

  LCD_WriteCommand(0xC0);
  LCD_WriteData(0x2C);

  LCD_WriteCommand(0xC2);
  LCD_WriteData(0x01);
  LCD_WriteData(0xFF);

  LCD_WriteCommand(0xC3);
  LCD_WriteData(0x13);

  LCD_WriteCommand(0xC4);
  LCD_WriteData(0x20);

  LCD_WriteCommand(0xC6);
  LCD_WriteData(0x0F);

  LCD_WriteCommand(0xD0);
  LCD_WriteData(0xA4);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xD6);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xE0);
  LCD_WriteData(0xD0);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x14);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x09);
  LCD_WriteData(0x38);
  LCD_WriteData(0x44);
  LCD_WriteData(0x4E);
  LCD_WriteData(0x3A);
  LCD_WriteData(0x17);
  LCD_WriteData(0x18);
  LCD_WriteData(0x2F);
  LCD_WriteData(0x30);

  LCD_WriteCommand(0xE1);
  LCD_WriteData(0xD0);
  LCD_WriteData(0x09);
  LCD_WriteData(0x0F);
  LCD_WriteData(0x08);
  LCD_WriteData(0x07);
  LCD_WriteData(0x14);
  LCD_WriteData(0x37);
  LCD_WriteData(0x44);
  LCD_WriteData(0x4D);
  LCD_WriteData(0x38);
  LCD_WriteData(0x15);
  LCD_WriteData(0x16);
  LCD_WriteData(0x2C);
  LCD_WriteData(0x2E);

  LCD_WriteCommand(0x21);

  LCD_WriteCommand(0x29);

  LCD_WriteCommand(0x2C);

  // ── Create the dual-core mutex ──────────────────────────────────
  lcd_fb_mutex = xSemaphoreCreateMutex();
  if (!lcd_fb_mutex) {
    Serial.println("[LCD] ERROR - mutex creation failed!");
  }

  // ── Allocate PSRAM framebuffer ──────────────────────────────────
  lcd_framebuffer = (uint16_t*)heap_caps_malloc(LCD_FB_SIZE, MALLOC_CAP_SPIRAM);
  if (lcd_framebuffer) {
    memset(lcd_framebuffer, 0x00, LCD_FB_SIZE);   // start with black
    Serial.printf("[LCD] PSRAM framebuffer allocated: %u bytes at %p\n",
                  (unsigned)LCD_FB_SIZE, lcd_framebuffer);
  } else {
    Serial.println("[LCD] ERROR - PSRAM framebuffer allocation failed!");
  }

  update_lcd_ = false;

  Touch_Init();
}

// ── Backlight ───────────────────────────────────────────────────────
uint8_t LCD_Backlight = 50;

void Backlight_Init()
{
  ledcAttach(LCD_Backlight_PIN, Frequency, Resolution);
  ledcWrite(LCD_Backlight_PIN, Dutyfactor);
  Set_Backlight(LCD_Backlight);
}

void Set_Backlight(uint8_t Light)
{
  if (Light > Backlight_MAX || Light < 0)
    printf("Set Backlight parameters in the range of 0 to 100 \r\n");
  else {
    uint32_t Backlight = Light * 10;
    if (Backlight == 1000)
      Backlight = 1024;
    ledcWrite(LCD_Backlight_PIN, Backlight);
  }
}

// =====================================================================
//  Rotation
// =====================================================================
//
//  MADCTL (0x36) bit layout:
//    Bit 7 (MY)  — Row address order
//    Bit 6 (MX)  — Column address order
//    Bit 5 (MV)  — Row/Column exchange
//
//  Rot 0: 0x00          portrait         240×320
//  Rot 1: 0x60 (MX|MV)  landscape        320×240
//  Rot 2: 0xC0 (MY|MX)  portrait inv.    240×320
//  Rot 3: 0xA0 (MY|MV)  landscape inv.   320×240
// =====================================================================

// MADCTL values for each rotation
static const uint8_t madctl_table[4] = { 0x00, 0x60, 0xC0, 0xA0 };

void LCD_SetRotation(uint8_t rotation)
{
  rotation &= 0x03;   // clamp to 0–3
  lcd_rotation = rotation;

  // Update effective dimensions
  if (rotation & 0x01) {          // landscape (1 or 3)
    lcd_width  = LCD_PANEL_H;     // 320
    lcd_height = LCD_PANEL_W;     // 240
  } else {                        // portrait (0 or 2)
    lcd_width  = LCD_PANEL_W;     // 240
    lcd_height = LCD_PANEL_H;     // 320
  }

  // Send MADCTL to the ST7789
  LCD_WriteCommand(0x36);
  LCD_WriteData(madctl_table[rotation]);

  // Clear the framebuffer — pixel layout changed
  if (lcd_framebuffer) {
    memset(lcd_framebuffer, 0x00, LCD_FB_SIZE);
  }

  // Update LVGL display resolution (no-op if LVGL not installed or not yet initialized)
  LVGL_SetRotation(rotation);

  Serial.printf("[LCD] Rotation %d → %dx%d  MADCTL=0x%02X\n",
                rotation, lcd_width, lcd_height, madctl_table[rotation]);
}

uint8_t LCD_GetRotation(void)
{
  return lcd_rotation;
}

// =====================================================================
//  Dual-core framebuffer locking
// =====================================================================
//
//  LCD_FB_Start() takes the mutex.  If LCD_Update() is currently
//  streaming, this call blocks until the SPI transfer finishes and
//  the mutex is released.  Once acquired, the caller owns the
//  framebuffer and may write freely.
//
//  LCD_FB_End() sets update_lcd_ = true (new frame ready) and
//  releases the mutex so LCD_Update() can pick it up.
//
//  LCD_Update() checks the flag, takes the mutex, streams, clears
//  the flag, and releases the mutex.
// =====================================================================

void LCD_FB_Start(void)
{
  if (!lcd_fb_mutex) return;
  // Block indefinitely until the mutex is available.
  // If LCD_Update() is mid-transfer this will wait until it finishes.
  xSemaphoreTake(lcd_fb_mutex, portMAX_DELAY);

  // Reset the dirty rectangle for this new frame
  lcd_dirty.dirty = false;
}

void LCD_FB_End(void)
{
  if (!lcd_fb_mutex) return;
  // Only signal an update if something was actually drawn
  if (lcd_dirty.dirty) {
    update_lcd_ = true;
  }
  xSemaphoreGive(lcd_fb_mutex); // release so LCD_Update() can grab it
}

// =====================================================================
//  LCD_Update  -  stream ONLY the dirty region to the ST7789
// =====================================================================
//  Uses CASET/RASET to set the address window to the dirty bounding
//  box, then sends only those pixels.  For a 50x30 button redraw this
//  sends 3,000 bytes instead of 153,600 — a 50× speedup.
//
//  Strips are used within the dirty region to stay under the ESP32
//  SPI DMA transfer limit (~64KB).
// =====================================================================
void LCD_Update(void)
{
  if (!update_lcd_ || !lcd_framebuffer || !lcd_fb_mutex) return;

  xSemaphoreTake(lcd_fb_mutex, portMAX_DELAY);

  if (!update_lcd_ || !lcd_dirty.dirty) {
    update_lcd_ = false;
    xSemaphoreGive(lcd_fb_mutex);
    return;
  }

  // Snapshot the dirty rect (it won't change while we hold the mutex)
  int16_t dx1 = lcd_dirty.x1;
  int16_t dy1 = lcd_dirty.y1;
  int16_t dx2 = lcd_dirty.x2;
  int16_t dy2 = lcd_dirty.y2;

  uint16_t dirtyW = (uint16_t)(dx2 - dx1 + 1);
  uint16_t dirtyH = (uint16_t)(dy2 - dy1 + 1);

  // Set the ST7789 address window to exactly the dirty region
  LCD_BeginWrite();
  LCD_SetAddrWindow_NoCS(
    (uint16_t)(dx1 + Offset_X), (uint16_t)(dy1 + Offset_Y),
    (uint16_t)(dx2 + Offset_X), (uint16_t)(dy2 + Offset_Y));

  // Pick strip height so each transfer stays well under 64KB DMA limit.
  // dirtyW * stripH * 2 bytes per strip.  For worst case 240px wide,
  // 32 rows = 15,360 bytes.
  constexpr uint16_t MAX_STRIP_H = 32;

  uint16_t y = 0;
  while (y < dirtyH) {
    uint16_t stripH = MAX_STRIP_H;
    if (y + stripH > dirtyH) stripH = dirtyH - y;

    uint16_t srcY = (uint16_t)(dy1 + y);

    if (dirtyW == LCD_WIDTH) {
      // Full-width dirty region: send directly from framebuffer (no copy needed)
      uint32_t offset = (uint32_t)srcY * LCD_WIDTH + dx1;
      uint32_t bytes  = (uint32_t)dirtyW * stripH * 2;
      LCDspi.transferBytes((uint8_t*)&lcd_framebuffer[offset], nullptr, bytes);
    } else {
      // Partial-width: copy each row's dirty slice into a contiguous
      // temp buffer since the framebuffer rows are 240px wide but we're
      // only sending dirtyW pixels per row.
      //
      // Static buffer sized for worst case: 320 * 32 * 2 = 20,480 bytes
      // Uses LCD_PANEL_H (320) since that's the max possible lcd_width in landscape.
      static uint8_t stripBuf[LCD_PANEL_H * MAX_STRIP_H * 2];

      uint8_t* dst = stripBuf;
      for (uint16_t row = 0; row < stripH; row++) {
        uint32_t srcOffset = (uint32_t)(srcY + row) * LCD_WIDTH + dx1;
        memcpy(dst, &lcd_framebuffer[srcOffset], dirtyW * 2);
        dst += dirtyW * 2;
      }

      uint32_t bytes = (uint32_t)dirtyW * stripH * 2;
      LCDspi.transferBytes(stripBuf, nullptr, bytes);
    }

    y += stripH;
  }

  LCD_EndWrite();

  update_lcd_ = false;

  xSemaphoreGive(lcd_fb_mutex);
}

// =====================================================================
//  Framebuffer drawing helpers
// =====================================================================
//  *** Call these ONLY between LCD_FB_Start() and LCD_FB_End() ***
// =====================================================================

void LCD_FB_Clear(uint16_t color)
{
  if (!lcd_framebuffer) return;

  dirty_expand(0, 0, LCD_WIDTH, LCD_HEIGHT);

  // Fast path for black (0x0000)
  if (color == 0x0000) {
    memset(lcd_framebuffer, 0, LCD_FB_SIZE);
    return;
  }

  // Fill first row, then memcpy-expand to remaining rows
  for (uint16_t x = 0; x < LCD_WIDTH; ++x) {
    lcd_framebuffer[x] = color;
  }
  for (uint16_t row = 1; row < LCD_HEIGHT; ++row) {
    memcpy(&lcd_framebuffer[(uint32_t)row * LCD_WIDTH],
           lcd_framebuffer,
           LCD_WIDTH * sizeof(uint16_t));
  }
}

void LCD_FB_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (!lcd_framebuffer) return;
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  dirty_expand(x, y, 1, 1);
  lcd_framebuffer[(uint32_t)y * LCD_WIDTH + x] = color;
}

void LCD_FB_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (!lcd_framebuffer) return;

  // Clip to screen
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
  if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

  dirty_expand(x, y, w, h);

  // Fill the first row of the rect
  uint32_t rowStart = (uint32_t)y * LCD_WIDTH + x;
  for (uint16_t i = 0; i < w; ++i) {
    lcd_framebuffer[rowStart + i] = color;
  }

  // Copy that row to the remaining rows
  for (uint16_t row = 1; row < h; ++row) {
    memcpy(&lcd_framebuffer[(uint32_t)(y + row) * LCD_WIDTH + x],
           &lcd_framebuffer[rowStart],
           w * sizeof(uint16_t));
  }
}

void LCD_FB_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
  LCD_FB_FillRect(x, y, w, 1, color);
}

void LCD_FB_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
  if (!lcd_framebuffer) return;
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
  if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

  dirty_expand(x, y, 1, h);

  for (uint16_t row = 0; row < h; ++row) {
    lcd_framebuffer[(uint32_t)(y + row) * LCD_WIDTH + x] = color;
  }
}

void LCD_FB_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  LCD_FB_DrawHLine(x, y, w, color);                // top
  LCD_FB_DrawHLine(x, (uint16_t)(y + h - 1), w, color);  // bottom
  LCD_FB_DrawVLine(x, y, h, color);                // left
  LCD_FB_DrawVLine((uint16_t)(x + w - 1), y, h, color);  // right
}

void LCD_FB_Blit(uint16_t dx, uint16_t dy,
                 uint16_t w, uint16_t h, const uint16_t* src)
{
  if (!lcd_framebuffer || !src) return;

  dirty_expand(dx, dy, w, h);

  for (uint16_t row = 0; row < h; ++row) {
    uint16_t ty = dy + row;
    if (ty >= LCD_HEIGHT) break;

    uint16_t copyW = w;
    if (dx + copyW > LCD_WIDTH) copyW = LCD_WIDTH - dx;

    memcpy(&lcd_framebuffer[(uint32_t)ty * LCD_WIDTH + dx],
           &src[(uint32_t)row * w],
           copyW * sizeof(uint16_t));
  }
}

void LCD_FB_MarkDirty(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  dirty_expand(x, y, w, h);
}

void LCD_FB_MarkFullDirty(void)
{
  dirty_expand(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

// ── Legacy test (bypasses framebuffer, kept for reference) ──────────
void LCD_TestSolidBackgroundStep_Streamed(void)
{
  uint32_t t = millis() / 12;
  uint8_t phase = (uint8_t)(t & 0xFF);
  uint8_t ramp = (phase < 128) ? (phase * 2) : ((255 - phase) * 2);
  uint16_t bg = (uint16_t)(((ramp & 0xF8) << 8) | ((ramp & 0xFC) << 3) | (ramp >> 3));

  constexpr uint16_t STRIP_H = 20;
  static uint16_t strip[LCD_PANEL_H * STRIP_H];  // max possible width = 320

  for (uint32_t i = 0; i < (uint32_t)LCD_WIDTH * STRIP_H; ++i) strip[i] = bg;

  LCD_BeginWrite();
  LCD_SetAddrWindow_NoCS(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

  uint16_t y = 0;
  while (y < LCD_HEIGHT) {
    uint16_t h = STRIP_H;
    if (y + h > LCD_HEIGHT) h = (uint16_t)(LCD_HEIGHT - y);

    uint32_t bytes = (uint32_t)LCD_WIDTH * h * 2;
    LCDspi.transferBytes((uint8_t*)strip, nullptr, bytes);

    y = (uint16_t)(y + h);
  }

  LCD_EndWrite();
}

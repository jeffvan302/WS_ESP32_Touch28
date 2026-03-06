# WS_ESP32_Touch28

Hardware abstraction library for the **Waveshare ESP32-S3 Touch 2.8"** development board. Copy preconfigured lv_conf.h to your project and include <WS_ESP32_Touch28.h> and LVGL will work.  Provides ready-to-use drivers for all onboard peripherals through a single header include.

[https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8)

## WIFI Notes
There is no added features for bluetooth or wifi, since the built in libraries for ESP32 is sufficient.  Depending on the project other libraries would be required.  The intent of this library is to make the specific hardware on the **Waveshare ESP32-S3 Touch 2.8"** easy to access.

## LVGL operations
LVGL library requires the configuration of the lv_conf.h file for the hardware you use.  Since this library enables the drivers for the Waveshare ESP32-S3-Touch-LCD-2.8 device, the lv_conf.h is pre-configured for this device.  You do need to copy the lv_conf.h from this library to your Sketch folder.

## Features

- **ST7789 240×320 LCD** — PSRAM-backed framebuffer with dirty-rectangle optimization and dual-core mutex safety
- **CST328 Capacitive Touch** — Interrupt-driven 5-point multitouch with a high-level gesture recognition system (tap, double-tap, swipe, drag, long-press)
- **PCM5101 I2S Audio** — MP3 playback from SD card via a dedicated FreeRTOS task
- **SD_MMC Card** — File I/O, directory operations, and hardware-accelerated JPG/PNG image loading
- **QMI8658 6-axis IMU** — Accelerometer + gyroscope with integrated orientation tracking (yaw/pitch/roll)
- **PCF85063 Real-Time Clock** — Date/time keeping, alarm support, epoch conversion
- **Battery Monitoring** — ADC voltage reading with rolling average and Li-ion percentage curve
- **Power Management** — Sleep, restart, and shutdown via power key
- **Optional LVGL v9 Integration** — Compiles to zero overhead when LVGL is not installed

## Hardware Pinout

| Peripheral | Pins |
|---|---|
| LCD SPI | MOSI=45, SCLK=40, CS=42, DC=41, RST=39 |
| LCD Backlight | GPIO 5 (PWM) |
| Touch I2C (Wire1) | SDA=1, SCL=3, INT=4, RST=2 |
| IMU I2C (Wire) | SDA=11, SCL=10 |
| RTC I2C (Wire) | SDA=11, SCL=10 (shared with IMU) |
| I2S Audio | BCLK=48, LRC=38, DOUT=47 |
| SD_MMC (1-bit) | CLK=14, CMD=17, D0=16, D3=21 |
| Battery ADC | GPIO 8 |
| Power Key | Input=6, Control=7 |

## Installation

### Method 1 — Copy to Libraries Folder

1. Copy the entire `WS_ESP32_Touch28/` folder into your Arduino `libraries/` directory (typically `~/Arduino/libraries/` or `Documents/Arduino/libraries/`).
2. Restart the Arduino IDE.
3. The library will appear under **Sketch → Include Library → WS_ESP32_Touch28**.

### Method 2 — ZIP Import

1. Compress the `WS_ESP32_Touch28/` folder into a `.zip` file.
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library…** and select the file.

### Dependencies

Install these via **Library Manager** (Sketch → Include Library → Manage Libraries):

- **JPEGDEC** by Larry Bank — required for JPG image loading
- **PNGdec** by Larry Bank — required for PNG image loading
- **ESP32-audioI2S** by Schreibfaul1 — required for MP3 audio playback

### LVGL (Optional)

1. Install **lvgl** via Library Manager.
2. That's it — the library bundles its own `lv_conf.h` in `src/` and LVGL auto-detects it at compile time via `__has_include(<lvgl.h>)`. No manual copying required.

## Quick Start

### Without LVGL

```cpp
#include "WS_ESP32_Touch28.h"

void setup() {
  Serial.begin(115200);
  WS_ESP32_Init();

  // Draw directly to the framebuffer
  LCD_FB_Start();
  LCD_FB_Clear(LCD_Color565(0, 0, 0));
  LCD_FB_FillRect(50, 50, 140, 80, LCD_Color565(255, 255, 255));
  LCD_FB_End();
}

void loop() {
  WS_ESP32_Loop();
  delay(10);
}
```

### With LVGL

```cpp
#include <lvgl.h>
#include "WS_ESP32_Touch28.h"

void setup() {
  Serial.begin(115200);
  WS_ESP32_Init();    // Automatically calls LVGL_Init()

  // Create LVGL widgets
  lv_obj_t * label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "Hello LVGL!");
  lv_obj_center(label);
}

void loop() {
  WS_ESP32_Loop();    // Runs LVGL timer handler + LCD update
  delay(10);
}
```

### Display Rotation

The display can be rotated at any time — during `setup()` or on-the-fly at runtime. Touch coordinates, framebuffer layout, and LVGL resolution all update automatically.

```cpp
// In setup(), after WS_ESP32_Init():
LCD_SetRotation(1);   // landscape 320×240

// Or change at runtime (e.g. in a touch callback):
LCD_SetRotation(0);   // back to portrait 240×320
```

| Rotation | Orientation | LCD_WIDTH | LCD_HEIGHT | MADCTL |
|---|---|---|---|---|
| 0 | Portrait | 240 | 320 | 0x00 |
| 1 | Landscape | 320 | 240 | 0x60 |
| 2 | Portrait inverted | 240 | 320 | 0xC0 |
| 3 | Landscape inverted | 320 | 240 | 0xA0 |

Note: `LCD_SetRotation()` clears the framebuffer (the pixel layout changes), so you'll need to redraw your content afterward. If LVGL is active, call `lv_obj_invalidate(lv_screen_active())` after rotation to force a full redraw.

## API Reference

### Master Functions

| Function | Description |
|---|---|
| `WS_ESP32_Init()` | Initialize all hardware. Call once in `setup()`. |
| `WS_ESP32_Loop()` | Process touch, IMU, power, LVGL (if enabled), and LCD update. Call every `loop()` iteration. |

### Display (Display_ST7789)

All drawing functions must be called between `LCD_FB_Start()` and `LCD_FB_End()` unless noted.

| Function | Description |
|---|---|
| `LCD_Init()` | Initialize LCD and touch hardware (called by **`WS_ESP32_Init`**). |
| `LCD_SetRotation(uint8_t r)` | Set display rotation at runtime (0–3). Updates dimensions, touch mapping, and LVGL. |
| `LCD_GetRotation()` | Get current rotation (0–3). |
| `Backlight_Init()` | Initialize PWM backlight (called by **`WS_ESP32_Init`**). |
| `Set_Backlight(uint8_t level)` | Set backlight brightness (0–100). |
| `LCD_FB_Start()` | Acquire framebuffer mutex — safe to draw after this. |
| `LCD_FB_End()` | Release mutex and mark frame dirty for LCD_Update. |
| `LCD_Update()` | Stream dirty regions to LCD via SPI (called by `WS_ESP32_Loop`). |
| `LCD_FB_Clear(uint16_t color)` | Fill entire screen with a color. |
| `LCD_FB_DrawPixel(x, y, color)` | Draw a single pixel. |
| `LCD_FB_FillRect(x, y, w, h, color)` | Fill a rectangle. |
| `LCD_FB_DrawRect(x, y, w, h, color)` | Draw a 1px rectangle outline. |
| `LCD_FB_DrawHLine(x, y, w, color)` | Draw a horizontal line. |
| `LCD_FB_DrawVLine(x, y, h, color)` | Draw a vertical line. |
| `LCD_FB_Blit(dx, dy, w, h, src)` | Copy an RGB565 buffer into the framebuffer. |
| `LCD_FB_MarkDirty(x, y, w, h)` | Manually mark a region dirty. |
| `LCD_FB_MarkFullDirty()` | Force full-screen update on next cycle. |
| `LCD_Color565(r, g, b)` | Convert 8-bit RGB to RGB565. |

**Constants:** `LCD_WIDTH` / `LCD_HEIGHT` (runtime, change with rotation), `LCD_PANEL_W` (240), `LCD_PANEL_H` (320)

### Touch Gestures (Touch_Gestures)

Define rectangular or circular zones, specify which gestures to detect, and register callbacks. The gesture engine runs inside `WS_ESP32_Loop()`.

```cpp
TouchZone btn = Touch_MakeRect(
  1,                              // zone ID (0–255)
  80, 200, 80, 40,                // x, y, w, h
  GESTURE_CLICK | GESTURE_LONG_PRESS,  // gestures to detect
  myCallback                      // void myCallback(TouchEvent& evt)
);
Touch_Gesture_AddZone(&btn);
```

**Gesture Flags** (combine with `|`):
`GESTURE_CLICK`, `GESTURE_DOUBLE_CLICK`, `GESTURE_SWIPE_UP`, `GESTURE_SWIPE_DOWN`, `GESTURE_SWIPE_LEFT`, `GESTURE_SWIPE_RIGHT`, `GESTURE_DRAG`, `GESTURE_LONG_PRESS`, `GESTURE_SWIPE_ANY`, `GESTURE_ALL`

**TouchEvent fields:** `zoneID`, `gesture`, `x`, `y`, `startX`, `startY`, `deltaX`, `deltaY`, `dragPhase` (DRAG_START/DRAG_MOVE/DRAG_END), `duration`

| Function | Description |
|---|---|
| `Touch_Gesture_AddZone(zone)` | Register a touch zone. |
| `Touch_Gesture_RemoveZone(id)` | Remove a zone by ID. |
| `Touch_Gesture_ClearZones()` | Remove all zones. |
| `Touch_Gesture_EnableZone(id, enabled)` | Enable/disable a zone. |
| `Touch_Gesture_MoveZone(id, x, y)` | Reposition a zone. |
| `Touch_Gesture_ResizeZone(id, w, h)` | Resize a zone. |
| `Touch_IsPressed()` | Is a finger currently touching the screen? |
| `Touch_GetRawXY(&x, &y)` | Get current touch position (any zone). |
| `Touch_MakeRect(id, x, y, w, h, detect, cb)` | Helper to create a rectangular zone. |
| `Touch_MakeCircle(id, cx, cy, r, detect, cb)` | Helper to create a circular zone. |

### SD Card (SD_Card)

| Function | Description |
|---|---|
| `SD_Init()` | Initialize SD_MMC in 1-bit mode (called by **`WS_ESP32_Init`**). |
| `SD_IsMounted(remount)` | Check if card is accessible. Set `remount=true` to auto-recover. |
| `SD_Exists(path)` | Check if file/directory exists. |
| `SD_FileSize(path)` | Get file size in bytes. |
| `SD_DeleteFile(path)` | Delete a file. |
| `SD_RenameFile(from, to)` | Rename/move a file. |
| `SD_CreateDir(path)` | Create a directory. |
| `SD_FindFile(dir, name)` | Search for a file in a directory. |
| `SD_ListFiles(dir, ext, array, max)` | List files by extension, returns count. |
| `SD_ReadTextFile(path, buf, size)` | Read text file into buffer. |
| `SD_ReadTextFileAlloc(path, &size)` | Read text file into PSRAM (caller frees). |
| `SD_WriteTextFile(path, content)` | Write/overwrite a text file. |
| `SD_AppendTextFile(path, content)` | Append to a text file. |
| `SD_ReadBinaryFile(path, buf, size)` | Read binary data. |
| `SD_WriteBinaryFile(path, data, len)` | Write binary data. |
| `SD_OpenFile(path, mode)` | Open file for streaming (returns `File`). |

**Image Loading** (requires JPEGDEC and/or PNGdec libraries):

| Function | Description |
|---|---|
| `SD_ImageInfo(path)` | Get image dimensions and type without decoding. |
| `SD_LoadJPG(path, fb, fbW, fbH, dx, dy, scale, rotation)` | Decode JPG into framebuffer. |
| `SD_LoadPNG(path, fb, fbW, fbH, dx, dy, scale, rotation)` | Decode PNG into framebuffer. |
| `SD_LoadImage(path, fb, fbW, fbH, dx, dy, scale, rotation)` | Auto-detect format and load. |
| `SD_LoadImageFit(path, fb, fbW, fbH, dx, dy, maxW, maxH, rot)` | Load and fit within bounds. |

**Rotation constants:** `IMG_ROTATE_0`, `IMG_ROTATE_90`, `IMG_ROTATE_180`, `IMG_ROTATE_270`

### Audio (Audio_PCM5101)

MP3 playback from SD card via I2S DAC. Audio runs on a dedicated FreeRTOS task for glitch-free playback.

| Function | Description |
|---|---|
| `Audio_Init()` | Initialize I2S and start audio task (called by **`WS_ESP32_Init`**). |
| `Play_Music(directory, fileName)` | Play an MP3 file from SD card. |
| `Play_Music_test()` | Play the test file `/music/test.mp3`. |
| `Volume_adjustment(vol)` | Set volume (0–21). |
| `Music_pause()` | Pause playback. |
| `Music_resume()` | Resume playback. |
| `Music_Duration()` | Get track duration in seconds. |
| `Music_Elapsed()` | Get elapsed time in seconds. |
| `Music_Energy()` | Get current audio energy level. |

### IMU / Gyroscope (Gyro_QMI8658)

6-axis accelerometer + gyroscope with automatic orientation tracking. Calibrates at startup (hold the device still for ~1 second).

| Function | Description |
|---|---|
| `QMI8658_Init()` | Initialize IMU (called by **`WS_ESP32_Init`**). |
| `QMI8658_Loop()` | Update sensor readings and orientation (called by `WS_ESP32_Loop`). |
| `getAccX()`, `getAccY()`, `getAccZ()` | Accelerometer axes (g). |
| `getGyroX()`, `getGyroY()`, `getGyroZ()` | Gyroscope axes (°/s). |
| `IMU_GetYaw()` | Heading in degrees (rotation about Z axis). |
| `IMU_GetPitch()` | Pitch in degrees (rotation about X axis). |
| `IMU_GetRoll()` | Roll in degrees (rotation about Y axis). |
| `IMU_ResetOrientation()` | Reset yaw/pitch/roll to zero. |

**Global structs:** `Accel` and `Gyro` (type `IMUdata` with `.x`, `.y`, `.z` fields).

### Battery (BAT_Driver)

| Function | Description |
|---|---|
| `BAT_Init()` | Initialize ADC (called by **`WS_ESP32_Init`**). |
| `BAT_Get_Volts()` | Read battery voltage (returns `float` in volts). |
| `BAT_voltageToPercent(float v)` | Convert voltage to 0–100% using a Li-ion discharge curve (piecewise linear, 11-point lookup table). |
| `BAT_store_volt_history()` | Store the current voltage into a rolling 20-sample buffer (called by `WS_ESP32_Loop`). |
| `BAT_read_avarage_Volts()` | Get the average voltage from the rolling buffer. Smooths out ADC noise. |

### RTC (RTC_PCF85063)

Real-time clock on I2C bus 0 (shared with IMU). Provides date/time keeping and alarm functionality. The `datetime` global struct is updated automatically by `PCF85063_Loop()` every main loop iteration.

| Function | Description |
|---|---|
| `PCF85063_Init()` | Initialize the RTC in 24-hour mode (called by **`WS_ESP32_Init`**). |
| `PCF85063_Loop()` | Read current date/time into the global `datetime` struct (called by `WS_ESP32_Loop`). |
| `PCF85063_Reset()` | Software reset the RTC. |
| `PCF85063_Set_Time(datetime_t t)` | Set hours, minutes, seconds. |
| `PCF85063_Set_Date(datetime_t d)` | Set year, month, day, day-of-week. |
| `PCF85063_Set_All(datetime_t t)` | Set date and time in one call. |
| `PCF85063_Read_Time(datetime_t* t)` | Read current date/time into a struct. |
| `PCF85063_Enable_Alarm()` | Enable alarm interrupt and clear alarm flag. |
| `PCF85063_Get_Alarm_Flag()` | Check if the alarm has triggered (returns flag byte). |
| `PCF85063_Set_Alarm(datetime_t t)` | Set alarm time (hours, minutes, seconds). |
| `PCF85063_Read_Alarm(datetime_t* t)` | Read the current alarm setting. |
| `datetime_to_str(char* buf, datetime_t t)` | Format a datetime as a human-readable string. |
| `datetimeToEpoch(datetime_t t)` | Convert datetime to Unix epoch seconds. |

**Global:** `datetime` (type `datetime_t`) — automatically updated every loop iteration. Fields: `year`, `month`, `day`, `dotw` (day of week, 0=Sunday), `hour`, `minute`, `second`.

### Power Management (PWR_Key)

| Function | Description |
|---|---|
| `PWR_Init()` | Initialize power key GPIO (called by **`WS_ESP32_Init`**). |
| `PWR_Loop()` | Poll power button (called by `WS_ESP32_Loop`). |
| `Fall_Asleep()` | Enter light sleep mode. |
| `Shutdown()` | Power off the device. |
| `Restart()` | Restart the ESP32. |

Long-press durations are configurable in `PWR_Key.h`: sleep (10s), restart (15s), shutdown (20s).

### LVGL Driver (LVGL_Driver)

Automatically detected at compile time. When LVGL is not installed, all functions compile to empty stubs with zero overhead.

| Function | Description |
|---|---|
| `LVGL_Init()` | Register LVGL display and touch drivers (called by **`WS_ESP32_Init`**). |
| `LVGL_Loop()` | Run `lv_timer_handler()` (called by `WS_ESP32_Loop`). |
| `LVGL_GetDisplay()` | Get the `lv_display_t*` for advanced configuration. |
| `LVGL_GetIndev()` | Get the `lv_indev_t*` for advanced configuration. |

**Compile-time check:** Use `#if HAS_LVGL` to conditionally include LVGL-specific code.

### I2C (I2C_Driver)

Low-level I2C helper for Wire bus 0 (IMU). Touch uses Wire1 separately.

| Function | Description |
|---|---|
| `I2C_Init()` | Initialize Wire bus 0 on SDA=11, SCL=10 (called by **`WS_ESP32_Init`**). |
| `I2C_Read(addr, reg, data, len)` | Read registers from an I2C device. |
| `I2C_Write(addr, reg, data, len)` | Write registers to an I2C device. |

## Examples

The library includes five example sketches (File → Examples → WS_ESP32_Touch28):

- **Basic_NoLVGL** — Framebuffer drawing and touch zones without LVGL
- **LVGL_HelloWorld** — Button and slider using LVGL widgets
- **Touch_Gestures_Demo** — All gesture types with visual feedback
- **SD_ImageViewer** — Browse JPG/PNG images with swipe navigation
- **IMU_Orientation** — Real-time yaw/pitch/roll tracking with tap-to-reset

## Architecture Notes

The library is designed around a shared PSRAM framebuffer. All drawing — whether from raw `LCD_FB_*` calls, SD card image loading, or LVGL widget rendering — writes into the same 153,600-byte buffer. A FreeRTOS mutex (`lcd_fb_mutex`) protects concurrent access from both CPU cores, and the `WS_ESP32_Loop()` performs the maintaining features for updating the screen by burst streaming to the ST7789 via SPI at 80 MHz.

LVGL is integrated as an optional overlay: its flush callback copies rendered pixels into the existing framebuffer and marks the affected region dirty, so the existing SPI pipeline handles all hardware communication. The touch input callback reads from the gesture layer's raw position API, letting LVGL handle its own gesture recognition internally.

Audio playback runs on a dedicated FreeRTOS task (priority 19, core 0) to ensure the I2S DMA buffer stays filled regardless of display or application processing load.

## License

See individual source files for license information.

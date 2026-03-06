/**
 * SD_ImageViewer — Browse and display images from SD card
 *
 * Demonstrates:
 *  - SD card file listing with extension filter
 *  - JPG and PNG image loading with fit-to-screen scaling
 *  - Swipe gestures to navigate between images
 *
 * Place .jpg or .png files in /images/ on the SD card.
 * No LVGL required.
 */

#include "WS_ESP32_Touch28.h"

#define MAX_IMAGES 50
static char imageFiles[MAX_IMAGES][100];
static uint16_t imageCount = 0;
static int16_t  currentImage = 0;

// ── Display current image ───────────────────────────────────────────
void showImage(int16_t index) {
  if (imageCount == 0) return;

  // Wrap around
  if (index < 0) index = imageCount - 1;
  if (index >= imageCount) index = 0;
  currentImage = index;

  char path[120];
  snprintf(path, sizeof(path), "/images/%s", imageFiles[currentImage]);

  Serial.printf("Showing: %s (%d/%d)\n", path, currentImage + 1, imageCount);

  LCD_FB_Start();
  LCD_FB_Clear(0x0000);
  SD_LoadImageFit(path,
                  lcd_framebuffer, LCD_WIDTH, LCD_HEIGHT,
                  LCD_WIDTH / 2, LCD_HEIGHT / 2,
                  LCD_WIDTH, LCD_HEIGHT,
                  IMG_ROTATE_0);
  LCD_FB_End();
}

// ── Swipe callback ──────────────────────────────────────────────────
static void onSwipe(TouchEvent& evt) {
  if (evt.gesture == GESTURE_SWIPE_LEFT) {
    showImage(currentImage + 1);
  } else if (evt.gesture == GESTURE_SWIPE_RIGHT) {
    showImage(currentImage - 1);
  }
}

void setup() {
  Serial.begin(115200);
  WS_ESP32_Init();

  LCD_FB_Start();
  LCD_FB_Clear(0x0000);
  LCD_FB_End();

  if (!SD_IsMounted(true)) {
    Serial.println("SD card not found!");
    return;
  }

  // Create /images/ directory if it doesn't exist
  if (!SD_Exists("/images")) {
    SD_CreateDir("/images");
    Serial.println("Created /images/ folder — add JPG/PNG files and reset.");
    return;
  }

  // List JPG files
  imageCount = SD_ListFiles("/images", ".jpg", imageFiles, MAX_IMAGES);

  // Also list PNG files
  char pngFiles[MAX_IMAGES][100];
  uint16_t pngCount = SD_ListFiles("/images", ".png", pngFiles, MAX_IMAGES - imageCount);
  for (uint16_t i = 0; i < pngCount && imageCount < MAX_IMAGES; i++) {
    strncpy(imageFiles[imageCount], pngFiles[i], 100);
    imageCount++;
  }

  Serial.printf("Found %d images\n", imageCount);

  if (imageCount > 0) {
    showImage(0);
  }

  // Full-screen swipe zone
  TouchZone swipeZone = Touch_MakeRect(
    1, 0, 0, LCD_WIDTH, LCD_HEIGHT,
    GESTURE_SWIPE_LEFT | GESTURE_SWIPE_RIGHT,
    onSwipe);
  Touch_Gesture_AddZone(&swipeZone);

  Serial.println("Swipe left/right to browse images");
}

void loop() {
  WS_ESP32_Loop();
  delay(10);
}

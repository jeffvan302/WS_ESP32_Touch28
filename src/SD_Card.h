#pragma once
#include "Arduino.h"
#include <cstring>
#include "FS.h"
#include "SD_MMC.h"
#include <esp_heap_caps.h>

// ── SD MMC Pin Definitions (1-bit mode) ─────────────────────────────
#define SD_CLK_PIN      14
#define SD_CMD_PIN      17
#define SD_D0_PIN       16
#define SD_D1_PIN       -1
#define SD_D2_PIN       -1
#define SD_D3_PIN       21

// ── Image rotation constants ────────────────────────────────────────
#define IMG_ROTATE_0     0
#define IMG_ROTATE_90    90
#define IMG_ROTATE_180   180
#define IMG_ROTATE_270   270

// ── Image scale mode constants ──────────────────────────────────────
#define IMG_SCALE_NONE   0    // Use scale factor as-is
#define IMG_SCALE_FIT    1    // Fit within target w/h, preserve aspect
#define IMG_SCALE_FILL   2    // Fill target w/h, crop excess

// ── Status / info ───────────────────────────────────────────────────
extern bool     SD_Card_Mounted;
extern uint16_t SD_Card_SizeMB;
extern uint16_t Flash_SizeMB;

// ── Image info struct (returned by SD_ImageInfo) ────────────────────
struct ImageInfo {
  uint16_t width;
  uint16_t height;
  uint8_t  type;        // 0=unknown, 1=JPG, 2=PNG
  bool     valid;
};

// ═══════════════════════════════════════════════════════════════════
//  CORE SD CARD
// ═══════════════════════════════════════════════════════════════════

// Initialize the SD card (1-bit SD_MMC). Returns true on success.
bool SD_Init(void);

// Unmount the SD card cleanly.
void SD_Deinit(void);

// Check if SD card is physically present and accessible.
// Performs a real read test against the card (not just a flag check).
// If remount=true and the card probe fails, attempts SD_Deinit + SD_Init
// to recover from a card that was removed and reinserted.
bool SD_IsMounted(bool remount = false);

// Get total / used / free bytes on the card.
uint64_t SD_TotalBytes(void);
uint64_t SD_UsedBytes(void);
uint64_t SD_FreeBytes(void);

// Get flash chip size in MB.
uint16_t SD_GetFlashSizeMB(void);

// ═══════════════════════════════════════════════════════════════════
//  FILE & DIRECTORY OPERATIONS
// ═══════════════════════════════════════════════════════════════════

// Check if a file or directory exists.
bool SD_Exists(const char* path);

// Get file size in bytes.  Returns 0 if not found.
size_t SD_FileSize(const char* path);

// Delete a file.  Returns true on success.
bool SD_DeleteFile(const char* path);

// Rename / move a file.
bool SD_RenameFile(const char* from, const char* to);

// Create a directory (and parents if needed).
bool SD_CreateDir(const char* path);

// Remove a directory.
bool SD_RemoveDir(const char* path);

// Search for a specific file name inside a directory (non-recursive).
bool SD_FindFile(const char* directory, const char* fileName);

// List files with a given extension in a directory.
// Fills File_Name[][100] and returns the count found.
uint16_t SD_ListFiles(const char* directory, const char* extension,
                      char fileNames[][100], uint16_t maxFiles);

// ═══════════════════════════════════════════════════════════════════
//  TEXT FILE I/O  (for JSON, XML, config files, etc.)
// ═══════════════════════════════════════════════════════════════════

// Read an entire text file into a caller-supplied buffer.
// Returns number of bytes read, or 0 on failure.
// Buffer will be null-terminated.
size_t SD_ReadTextFile(const char* path, char* buffer, size_t bufferSize);

// Read an entire text file into a PSRAM-allocated buffer.
// Caller must free() the returned pointer when done.
// Sets *outSize to the file size (excluding null terminator).
// Returns nullptr on failure.
char* SD_ReadTextFileAlloc(const char* path, size_t* outSize);

// Write a null-terminated string to a file (overwrite).
bool SD_WriteTextFile(const char* path, const char* content);

// Append a null-terminated string to a file.
bool SD_AppendTextFile(const char* path, const char* content);

// ═══════════════════════════════════════════════════════════════════
//  BINARY FILE I/O  (for MP3, raw data, firmware, etc.)
// ═══════════════════════════════════════════════════════════════════

// Read up to bufferSize bytes from a file.
// Returns the number of bytes actually read.
size_t SD_ReadBinaryFile(const char* path, uint8_t* buffer, size_t bufferSize);

// Write binary data to a file (overwrite).
bool SD_WriteBinaryFile(const char* path, const uint8_t* data, size_t length);

// Open a file for streaming access.  Caller manages the File object.
// mode: FILE_READ, FILE_WRITE, or FILE_APPEND.
File SD_OpenFile(const char* path, const char* mode);

// ═══════════════════════════════════════════════════════════════════
//  IMAGE LOADING  (JPG / PNG  →  RGB565 framebuffer)
//
//  Requires Arduino libraries (install via Library Manager):
//    - JPEGDEC   by Larry Bank
//    - PNGdec    by Larry Bank
//
//  All image functions decode from SD card, apply optional
//  scaling + rotation, and blit the result into an RGB565 buffer
//  (typically your lcd_framebuffer from Display_ST7789).
// ═══════════════════════════════════════════════════════════════════

// Get image dimensions and type without fully decoding.
ImageInfo SD_ImageInfo(const char* path);

// Load a JPG file and draw it into the framebuffer.
//   fb / fbWidth / fbHeight : target RGB565 buffer and its dimensions
//   destX, destY            : where to place the top-left corner
//   scale                   : 1.0 = original, 0.5 = half, 2.0 = double, etc.
//   rotation                : IMG_ROTATE_0 / 90 / 180 / 270
// Returns true on success.
bool SD_LoadJPG(const char* path,
                uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int16_t destX, int16_t destY,
                float scale, uint16_t rotation);

// Load a PNG file and draw it into the framebuffer.
// Same parameters as SD_LoadJPG.
bool SD_LoadPNG(const char* path,
                uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int16_t destX, int16_t destY,
                float scale, uint16_t rotation);

// Auto-detect JPG or PNG and load.  Convenience wrapper.
bool SD_LoadImage(const char* path,
                  uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                  int16_t destX, int16_t destY,
                  float scale, uint16_t rotation);

// Load an image and scale it to fit within maxW x maxH,
// preserving aspect ratio, centered at (destX, destY).
bool SD_LoadImageFit(const char* path,
                     uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                     int16_t destX, int16_t destY,
                     uint16_t maxW, uint16_t maxH,
                     uint16_t rotation);

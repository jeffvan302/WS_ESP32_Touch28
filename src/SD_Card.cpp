#include "SD_Card.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

// ── Optional image decoder libraries ────────────────────────────────
// Install via Arduino Library Manager:
//   JPEGDEC  by Larry Bank
//   PNGdec   by Larry Bank
// If not installed, image functions will return false gracefully.
#if __has_include(<JPEGDEC.h>)
  #include <JPEGDEC.h>
  #define HAS_JPEGDEC 1
#else
  #define HAS_JPEGDEC 0
#endif

#if __has_include(<PNGdec.h>)
  #include <PNGdec.h>
  #define HAS_PNGDEC 1
#else
  #define HAS_PNGDEC 0
#endif

// ── Globals ─────────────────────────────────────────────────────────
bool     SD_Card_Mounted = false;
uint16_t SD_Card_SizeMB  = 0;
uint16_t Flash_SizeMB    = 0;

// ── Internal: case-insensitive extension match ──────────────────────
static bool ext_match(const char* filename, const char* ext)
{
  if (!ext || ext[0] == '\0') return true;  // no filter = match all
  size_t nameLen = strlen(filename);
  size_t extLen  = strlen(ext);
  if (extLen > nameLen) return false;
  return (strcasecmp(filename + nameLen - extLen, ext) == 0);
}

// ── Internal: D3 pin control for 1-bit SD_MMC ───────────────────────
static void SD_D3_Disable() {
  digitalWrite(SD_D3_PIN, LOW);
  vTaskDelay(pdMS_TO_TICKS(10));
}

static void SD_D3_Enable() {
  digitalWrite(SD_D3_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(10));
}

// ── Internal: physically probe the SD card ──────────────────────────
// SD_MMC.end() only unmounts the FAT filesystem — the SDMMC host
// peripheral keeps its cached card state, so cardType(), exists(),
// and even a fresh begin() all return stale data after card removal.
//
// The fix: call sdmmc_host_deinit() to fully tear down the SDMMC
// hardware, then re-init from scratch.  This forces CMD0/CMD8/ACMD41
// on the bus — if no card answers, begin() fails.  ~100ms cost.
static bool SD_CardProbe(void)
{
  // Suppress SDMMC error spam during probe
  esp_log_level_set("sdmmc_req",    ESP_LOG_NONE);
  esp_log_level_set("sdmmc_cmd",    ESP_LOG_NONE);
  esp_log_level_set("diskio_sdmmc", ESP_LOG_NONE);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);

  // Tear down everything: filesystem, VFS, and the SDMMC host peripheral
  SD_MMC.end();
  sdmmc_host_deinit();
  vTaskDelay(pdMS_TO_TICKS(50));

  // Re-init from scratch
  pinMode(SD_D3_PIN, OUTPUT);
  SD_D3_Enable();

  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, -1, -1, -1);
  bool ok = SD_MMC.begin("/sdcard", true, false);  // formatOnFail=false

  // Restore logging
  esp_log_level_set("sdmmc_req",    ESP_LOG_ERROR);
  esp_log_level_set("sdmmc_cmd",    ESP_LOG_ERROR);
  esp_log_level_set("diskio_sdmmc", ESP_LOG_ERROR);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_ERROR);

  if (!ok) {
    SD_Card_Mounted = false;
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    SD_Card_Mounted = false;
    return false;
  }

  SD_Card_Mounted = true;
  SD_Card_SizeMB = (uint16_t)(SD_MMC.totalBytes() / (1024ULL * 1024ULL));
  return true;
}

// ═══════════════════════════════════════════════════════════════════
//  CORE SD CARD
// ═══════════════════════════════════════════════════════════════════

bool SD_Init(void)
{
  pinMode(SD_D3_PIN, OUTPUT);

  if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, -1, -1, -1)) {
    Serial.println("[SD] Pin configuration failed");
    SD_Card_Mounted = false;
    return false;
  }

  SD_D3_Enable();

  // 1-bit mode: mount point "/sdcard", mode1bit=true, formatOnFail=true
  if (!SD_MMC.begin("/sdcard", true, true)) {
    Serial.println("[SD] Mount failed");
    SD_Card_Mounted = false;
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] No card detected");
    SD_Card_Mounted = false;
    return false;
  }

  SD_Card_Mounted = true;

  const char* typeStr = "UNKNOWN";
  if (cardType == CARD_MMC)  typeStr = "MMC";
  if (cardType == CARD_SD)   typeStr = "SDSC";
  if (cardType == CARD_SDHC) typeStr = "SDHC";

  uint64_t total = SD_MMC.totalBytes();
  uint64_t used  = SD_MMC.usedBytes();
  SD_Card_SizeMB = (uint16_t)(total / (1024ULL * 1024ULL));

  Serial.printf("[SD] Card: %s  Total: %lluMB  Used: %lluMB  Free: %lluMB\n",
                typeStr,
                total / (1024ULL * 1024ULL),
                used  / (1024ULL * 1024ULL),
                (total - used) / (1024ULL * 1024ULL));

  return true;
}

void SD_Deinit(void)
{
  SD_MMC.end();
  SD_D3_Disable();
  SD_Card_Mounted = false;
  Serial.println("[SD] Unmounted");
}

bool SD_IsMounted(bool remount)
{
  // Fast path: flag says not mounted, and no remount requested
  if (!SD_Card_Mounted && !remount) return false;

  // Probe does full SDMMC host teardown + re-init (~100ms)
  // This is the only reliable way to detect card presence on ESP32
  bool present = SD_CardProbe();

  if (!present && !remount) {
    Serial.println("[SD] Card not detected");
  }
  // If remount=true, SD_CardProbe already attempted the remount.
  // If it still failed, the card truly isn't there.
  if (!present && remount) {
    Serial.println("[SD] Card not detected (remount failed)");
  }
  if (present && remount && !SD_Card_Mounted) {
    // This shouldn't happen since CardProbe sets the flag, but just in case
    SD_Card_Mounted = true;
  }

  return present;
}

uint64_t SD_TotalBytes(void)  { return SD_Card_Mounted ? SD_MMC.totalBytes() : 0; }
uint64_t SD_UsedBytes(void)   { return SD_Card_Mounted ? SD_MMC.usedBytes()  : 0; }
uint64_t SD_FreeBytes(void)   { return SD_TotalBytes() - SD_UsedBytes(); }

uint16_t SD_GetFlashSizeMB(void)
{
  Flash_SizeMB = (uint16_t)(ESP.getFlashChipSize() / (1024 * 1024));
  return Flash_SizeMB;
}

// ═══════════════════════════════════════════════════════════════════
//  FILE & DIRECTORY OPERATIONS
// ═══════════════════════════════════════════════════════════════════

bool SD_Exists(const char* path)
{
  if (!SD_Card_Mounted) return false;
  return SD_MMC.exists(path);
}

size_t SD_FileSize(const char* path)
{
  if (!SD_Card_Mounted) return 0;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

bool SD_DeleteFile(const char* path)
{
  if (!SD_Card_Mounted) return false;
  return SD_MMC.remove(path);
}

bool SD_RenameFile(const char* from, const char* to)
{
  if (!SD_Card_Mounted) return false;
  return SD_MMC.rename(from, to);
}

bool SD_CreateDir(const char* path)
{
  if (!SD_Card_Mounted) return false;
  return SD_MMC.mkdir(path);
}

bool SD_RemoveDir(const char* path)
{
  if (!SD_Card_Mounted) return false;
  return SD_MMC.rmdir(path);
}

bool SD_FindFile(const char* directory, const char* fileName)
{
  if (!SD_Card_Mounted) return false;

  File dir = SD_MMC.open(directory);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("[SD] Directory not found: %s\n", directory);
    return false;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (strcmp(entry.name(), fileName) == 0) {
      entry.close();
      dir.close();
      return true;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return false;
}

uint16_t SD_ListFiles(const char* directory, const char* extension,
                      char fileNames[][100], uint16_t maxFiles)
{
  if (!SD_Card_Mounted) return 0;

  File dir = SD_MMC.open(directory);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("[SD] Directory not found: %s\n", directory);
    return 0;
  }

  uint16_t count = 0;
  File entry = dir.openNextFile();

  while (entry && count < maxFiles) {
    if (!entry.isDirectory()) {
      if (ext_match(entry.name(), extension)) {
        strncpy(fileNames[count], entry.name(), 99);
        fileNames[count][99] = '\0';
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  return count;
}

// ═══════════════════════════════════════════════════════════════════
//  TEXT FILE I/O
// ═══════════════════════════════════════════════════════════════════

size_t SD_ReadTextFile(const char* path, char* buffer, size_t bufferSize)
{
  if (!SD_Card_Mounted || !buffer || bufferSize == 0) return 0;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[SD] Cannot open: %s\n", path);
    return 0;
  }

  size_t toRead = f.size();
  if (toRead >= bufferSize) toRead = bufferSize - 1;  // leave room for null

  size_t bytesRead = f.read((uint8_t*)buffer, toRead);
  buffer[bytesRead] = '\0';
  f.close();
  return bytesRead;
}

char* SD_ReadTextFileAlloc(const char* path, size_t* outSize)
{
  if (outSize) *outSize = 0;
  if (!SD_Card_Mounted) return nullptr;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[SD] Cannot open: %s\n", path);
    return nullptr;
  }

  size_t fileSize = f.size();
  // Allocate in PSRAM if available, else internal RAM
  char* buf = (char*)heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) {
    buf = (char*)malloc(fileSize + 1);
  }
  if (!buf) {
    Serial.printf("[SD] Alloc failed for %u bytes\n", (unsigned)(fileSize + 1));
    f.close();
    return nullptr;
  }

  size_t bytesRead = f.read((uint8_t*)buf, fileSize);
  buf[bytesRead] = '\0';
  f.close();

  if (outSize) *outSize = bytesRead;
  return buf;
}

bool SD_WriteTextFile(const char* path, const char* content)
{
  if (!SD_Card_Mounted || !content) return false;

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] Cannot create: %s\n", path);
    return false;
  }

  size_t written = f.print(content);
  f.close();
  return (written > 0);
}

bool SD_AppendTextFile(const char* path, const char* content)
{
  if (!SD_Card_Mounted || !content) return false;

  File f = SD_MMC.open(path, FILE_APPEND);
  if (!f) {
    Serial.printf("[SD] Cannot open for append: %s\n", path);
    return false;
  }

  size_t written = f.print(content);
  f.close();
  return (written > 0);
}

// ═══════════════════════════════════════════════════════════════════
//  BINARY FILE I/O
// ═══════════════════════════════════════════════════════════════════

size_t SD_ReadBinaryFile(const char* path, uint8_t* buffer, size_t bufferSize)
{
  if (!SD_Card_Mounted || !buffer || bufferSize == 0) return 0;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return 0;

  size_t toRead = f.size();
  if (toRead > bufferSize) toRead = bufferSize;

  size_t bytesRead = f.read(buffer, toRead);
  f.close();
  return bytesRead;
}

bool SD_WriteBinaryFile(const char* path, const uint8_t* data, size_t length)
{
  if (!SD_Card_Mounted || !data) return false;

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;

  size_t written = f.write(data, length);
  f.close();
  return (written == length);
}

File SD_OpenFile(const char* path, const char* mode)
{
  if (!SD_Card_Mounted) return File();
  return SD_MMC.open(path, mode);
}

// ═══════════════════════════════════════════════════════════════════
//  INTERNAL: Image helper — detect type by file header
// ═══════════════════════════════════════════════════════════════════

static uint8_t detect_image_type(const char* path)
{
  // 0=unknown, 1=JPG, 2=PNG
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return 0;

  uint8_t header[8];
  if (f.read(header, 8) < 8) { f.close(); return 0; }
  f.close();

  // JPG: starts with FF D8 FF
  if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) return 1;

  // PNG: starts with 89 50 4E 47 0D 0A 1A 0A
  if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) return 2;

  // Fallback: check extension
  const char* dot = strrchr(path, '.');
  if (dot) {
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return 1;
    if (strcasecmp(dot, ".png") == 0) return 2;
  }
  return 0;
}

// ═══════════════════════════════════════════════════════════════════
//  INTERNAL: Load entire file into a PSRAM buffer
// ═══════════════════════════════════════════════════════════════════

static uint8_t* load_file_to_psram(const char* path, size_t* outSize)
{
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { *outSize = 0; return nullptr; }

  size_t sz = f.size();
  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!buf) {
    Serial.printf("[SD] PSRAM alloc failed: %u bytes\n", (unsigned)sz);
    f.close();
    *outSize = 0;
    return nullptr;
  }

  f.read(buf, sz);
  f.close();
  *outSize = sz;
  return buf;
}

// ═══════════════════════════════════════════════════════════════════
//  INTERNAL: Scaled + rotated blit from temp buffer to framebuffer
// ═══════════════════════════════════════════════════════════════════
//
//  srcBuf:  decoded image in RGB565, dimensions srcW x srcH
//  fb:      destination framebuffer, dimensions fbW x fbH
//  destX/Y: top-left corner in fb for the output image
//  scale:   output scaling (1.0 = original)
//  rot:     0 / 90 / 180 / 270
//
//  Rotation is applied first (CW), then scaling.

static void blit_scaled_rotated(
    const uint16_t* srcBuf, uint16_t srcW, uint16_t srcH,
    uint16_t* fb, uint16_t fbW, uint16_t fbH,
    int16_t destX, int16_t destY,
    float scale, uint16_t rot)
{
  // Output dimensions after rotation (before scaling)
  uint16_t rotW, rotH;
  if (rot == 90 || rot == 270) {
    rotW = srcH;
    rotH = srcW;
  } else {
    rotW = srcW;
    rotH = srcH;
  }

  // Final output dimensions after scaling
  uint16_t outW = (uint16_t)(rotW * scale + 0.5f);
  uint16_t outH = (uint16_t)(rotH * scale + 0.5f);
  if (outW == 0) outW = 1;
  if (outH == 0) outH = 1;

  // Inverse scale factor for mapping back
  float invScale = 1.0f / scale;

  for (uint16_t oy = 0; oy < outH; oy++) {
    int16_t fbY = destY + (int16_t)oy;
    if (fbY < 0) continue;
    if (fbY >= fbH) break;

    for (uint16_t ox = 0; ox < outW; ox++) {
      int16_t fbX = destX + (int16_t)ox;
      if (fbX < 0) continue;
      if (fbX >= fbW) break;

      // Map output pixel back through scale to rotated-image coords
      uint16_t rx = (uint16_t)(ox * invScale);
      uint16_t ry = (uint16_t)(oy * invScale);
      if (rx >= rotW) rx = rotW - 1;
      if (ry >= rotH) ry = rotH - 1;

      // Reverse rotation to get source coords
      uint16_t sx, sy;
      switch (rot) {
        case 90:
          sx = ry;
          sy = (srcH - 1) - rx;
          break;
        case 180:
          sx = (srcW - 1) - rx;
          sy = (srcH - 1) - ry;
          break;
        case 270:
          sx = (srcW - 1) - ry;
          sy = rx;
          break;
        default: // 0
          sx = rx;
          sy = ry;
          break;
      }

      if (sx < srcW && sy < srcH) {
        fb[(uint32_t)fbY * fbW + fbX] = srcBuf[(uint32_t)sy * srcW + sx];
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
//  JPEG DECODING
// ═══════════════════════════════════════════════════════════════════

#if HAS_JPEGDEC

// Context passed through the JPEGDEC user pointer
struct JPEGContext {
  uint16_t* tempBuf;
  uint16_t  imgW;
  uint16_t  imgH;
};

// JPEGDEC draw callback: copy each decoded MCU block into our temp buffer
static int jpeg_draw_cb(JPEGDRAW* pDraw)
{
  JPEGContext* ctx = (JPEGContext*)pDraw->pUser;
  if (!ctx || !ctx->tempBuf) return 0;

  uint16_t* src = pDraw->pPixels;
  for (int y = 0; y < pDraw->iHeight; y++) {
    int destY = pDraw->y + y;
    if (destY < 0 || destY >= ctx->imgH) { src += pDraw->iWidth; continue; }

    for (int x = 0; x < pDraw->iWidth; x++) {
      int destX = pDraw->x + x;
      if (destX >= 0 && destX < ctx->imgW) {
        ctx->tempBuf[(uint32_t)destY * ctx->imgW + destX] = src[x];
      }
    }
    src += pDraw->iWidth;
  }
  return 1;  // continue decoding
}

static bool decode_jpg_to_buffer(const uint8_t* fileData, size_t fileSize,
                                 uint16_t** outBuf, uint16_t* outW, uint16_t* outH)
{
  JPEGDEC jpeg;
  if (!jpeg.openRAM((uint8_t*)fileData, fileSize, jpeg_draw_cb)) {
    Serial.println("[SD] JPEG open failed");
    return false;
  }

  *outW = jpeg.getWidth();
  *outH = jpeg.getHeight();

  uint32_t bufBytes = (uint32_t)(*outW) * (*outH) * sizeof(uint16_t);
  *outBuf = (uint16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  if (!*outBuf) {
    Serial.printf("[SD] PSRAM alloc failed for JPEG: %ux%u (%u bytes)\n",
                  *outW, *outH, (unsigned)bufBytes);
    jpeg.close();
    return false;
  }

  JPEGContext ctx;
  ctx.tempBuf = *outBuf;
  ctx.imgW    = *outW;
  ctx.imgH    = *outH;
  jpeg.setUserPointer(&ctx);
  jpeg.setPixelType(RGB565_BIG_ENDIAN);

  if (!jpeg.decode(0, 0, 0)) {
    Serial.println("[SD] JPEG decode failed");
    heap_caps_free(*outBuf);
    *outBuf = nullptr;
    jpeg.close();
    return false;
  }

  jpeg.close();
  return true;
}

#endif // HAS_JPEGDEC

// ═══════════════════════════════════════════════════════════════════
//  PNG DECODING
// ═══════════════════════════════════════════════════════════════════

#if HAS_PNGDEC

// File-scope PNG object so the draw callback can call getLineAsRGB565()
static PNG _png_decoder;

// Context passed through PNGdec user pointer
struct PNGContext {
  uint16_t* tempBuf;
  uint16_t  imgW;
  uint16_t  imgH;
};

// PNGdec draw callback: called once per decoded scanline
static void png_draw_cb(PNGDRAW* pDraw)
{
  PNGContext* ctx = (PNGContext*)pDraw->pUser;
  if (!ctx || !ctx->tempBuf) return;
  if (pDraw->y >= ctx->imgH) return;

  // Convert the scanline to RGB565 using the PNGdec helper
  uint16_t lineBuf[1024];  // supports images up to 1024px wide
  uint16_t w = (pDraw->iWidth < 1024) ? pDraw->iWidth : 1024;

  _png_decoder.getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);

  uint32_t rowOffset = (uint32_t)pDraw->y * ctx->imgW;
  memcpy(&ctx->tempBuf[rowOffset], lineBuf, w * sizeof(uint16_t));
}

static bool decode_png_to_buffer(const uint8_t* fileData, size_t fileSize,
                                 uint16_t** outBuf, uint16_t* outW, uint16_t* outH)
{
  int rc = _png_decoder.openRAM((uint8_t*)fileData, fileSize, png_draw_cb);
  if (rc != PNG_SUCCESS) {
    Serial.printf("[SD] PNG open failed: %d\n", rc);
    return false;
  }

  *outW = _png_decoder.getWidth();
  *outH = _png_decoder.getHeight();

  if (*outW > 1024) {
    Serial.printf("[SD] PNG too wide: %u (max 1024)\n", *outW);
    _png_decoder.close();
    return false;
  }

  uint32_t bufBytes = (uint32_t)(*outW) * (*outH) * sizeof(uint16_t);
  *outBuf = (uint16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  if (!*outBuf) {
    Serial.printf("[SD] PSRAM alloc failed for PNG: %ux%u (%u bytes)\n",
                  *outW, *outH, (unsigned)bufBytes);
    _png_decoder.close();
    return false;
  }

  PNGContext ctx;
  ctx.tempBuf = *outBuf;
  ctx.imgW    = *outW;
  ctx.imgH    = *outH;
  _png_decoder.setUserPointer(&ctx);

  rc = _png_decoder.decode(NULL, 0);
  if (rc != PNG_SUCCESS) {
    Serial.printf("[SD] PNG decode failed: %d\n", rc);
    heap_caps_free(*outBuf);
    *outBuf = nullptr;
    _png_decoder.close();
    return false;
  }

  _png_decoder.close();
  return true;
}

#endif // HAS_PNGDEC

// ═══════════════════════════════════════════════════════════════════
//  PUBLIC IMAGE API
// ═══════════════════════════════════════════════════════════════════

ImageInfo SD_ImageInfo(const char* path)
{
  ImageInfo info = { 0, 0, 0, false };
  if (!SD_Card_Mounted) return info;

  info.type = detect_image_type(path);
  if (info.type == 0) return info;

  size_t fileSize;
  uint8_t* fileData = load_file_to_psram(path, &fileSize);
  if (!fileData) return info;

#if HAS_JPEGDEC
  if (info.type == 1) {
    JPEGDEC jpeg;
    if (jpeg.openRAM(fileData, fileSize, nullptr)) {
      info.width  = jpeg.getWidth();
      info.height = jpeg.getHeight();
      info.valid  = true;
      jpeg.close();
    }
  }
#endif

#if HAS_PNGDEC
  if (info.type == 2) {
    PNG png_local;
    if (png_local.openRAM(fileData, fileSize, nullptr) == PNG_SUCCESS) {
      info.width  = png_local.getWidth();
      info.height = png_local.getHeight();
      info.valid  = true;
      png_local.close();
    }
  }
#endif

  heap_caps_free(fileData);
  return info;
}

bool SD_LoadJPG(const char* path,
                uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int16_t destX, int16_t destY,
                float scale, uint16_t rotation)
{
#if HAS_JPEGDEC
  if (!SD_Card_Mounted || !fb) return false;

  size_t fileSize;
  uint8_t* fileData = load_file_to_psram(path, &fileSize);
  if (!fileData) return false;

  uint16_t* decoded = nullptr;
  uint16_t imgW = 0, imgH = 0;
  bool ok = decode_jpg_to_buffer(fileData, fileSize, &decoded, &imgW, &imgH);
  heap_caps_free(fileData);

  if (!ok) return false;

  blit_scaled_rotated(decoded, imgW, imgH, fb, fbWidth, fbHeight,
                      destX, destY, scale, rotation);

  heap_caps_free(decoded);
  return true;
#else
  Serial.println("[SD] JPEGDEC library not installed");
  return false;
#endif
}

bool SD_LoadPNG(const char* path,
                uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                int16_t destX, int16_t destY,
                float scale, uint16_t rotation)
{
#if HAS_PNGDEC
  if (!SD_Card_Mounted || !fb) return false;

  size_t fileSize;
  uint8_t* fileData = load_file_to_psram(path, &fileSize);
  if (!fileData) return false;

  uint16_t* decoded = nullptr;
  uint16_t imgW = 0, imgH = 0;
  bool ok = decode_png_to_buffer(fileData, fileSize, &decoded, &imgW, &imgH);
  heap_caps_free(fileData);

  if (!ok) return false;

  blit_scaled_rotated(decoded, imgW, imgH, fb, fbWidth, fbHeight,
                      destX, destY, scale, rotation);

  heap_caps_free(decoded);
  return true;
#else
  Serial.println("[SD] PNGdec library not installed");
  return false;
#endif
}

bool SD_LoadImage(const char* path,
                  uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                  int16_t destX, int16_t destY,
                  float scale, uint16_t rotation)
{
  if (!SD_Card_Mounted) return false;

  uint8_t type = detect_image_type(path);
  if (type == 1) return SD_LoadJPG(path, fb, fbWidth, fbHeight, destX, destY, scale, rotation);
  if (type == 2) return SD_LoadPNG(path, fb, fbWidth, fbHeight, destX, destY, scale, rotation);

  Serial.printf("[SD] Unknown image type: %s\n", path);
  return false;
}

bool SD_LoadImageFit(const char* path,
                     uint16_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                     int16_t destX, int16_t destY,
                     uint16_t maxW, uint16_t maxH,
                     uint16_t rotation)
{
  ImageInfo info = SD_ImageInfo(path);
  if (!info.valid) return false;

  // After rotation, effective source dimensions swap for 90/270
  uint16_t effW = (rotation == 90 || rotation == 270) ? info.height : info.width;
  uint16_t effH = (rotation == 90 || rotation == 270) ? info.width  : info.height;

  // Compute scale to fit within maxW x maxH, preserving aspect ratio
  float scaleX = (float)maxW / (float)effW;
  float scaleY = (float)maxH / (float)effH;
  float scale  = (scaleX < scaleY) ? scaleX : scaleY;
  if (scale > 1.0f) scale = 1.0f;  // don't upscale

  // Center within the max area
  uint16_t outW = (uint16_t)(effW * scale + 0.5f);
  uint16_t outH = (uint16_t)(effH * scale + 0.5f);
  int16_t cx = destX + (int16_t)((maxW - outW) / 2);
  int16_t cy = destY + (int16_t)((maxH - outH) / 2);

  return SD_LoadImage(path, fb, fbWidth, fbHeight, cx, cy, scale, rotation);
}

#include "SD_Card.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

// ── Globals ─────────────────────────────────────────────────────────
bool SD_Card_Mounted = false;
uint16_t SD_Card_SizeMB = 0;
uint16_t Flash_SizeMB = 0;

// ── Internal: case-insensitive extension match ──────────────────────
static bool ext_match(const char *filename, const char *ext)
{
  if (!ext || ext[0] == '\0')
    return true; // no filter = match all
  size_t nameLen = strlen(filename);
  size_t extLen = strlen(ext);
  if (extLen > nameLen)
    return false;
  return (strcasecmp(filename + nameLen - extLen, ext) == 0);
}

// ── Internal: D3 pin control for 1-bit SD_MMC ───────────────────────
static void SD_D3_Disable()
{
  digitalWrite(SD_D3_PIN, LOW);
  vTaskDelay(pdMS_TO_TICKS(10));
}

static void SD_D3_Enable()
{
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
  esp_log_level_set("sdmmc_req", ESP_LOG_NONE);
  esp_log_level_set("sdmmc_cmd", ESP_LOG_NONE);
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
  bool ok = SD_MMC.begin("/sdcard", true, false); // formatOnFail=false

  // Restore logging
  esp_log_level_set("sdmmc_req", ESP_LOG_ERROR);
  esp_log_level_set("sdmmc_cmd", ESP_LOG_ERROR);
  esp_log_level_set("diskio_sdmmc", ESP_LOG_ERROR);
  esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_ERROR);

  if (!ok)
  {
    SD_Card_Mounted = false;
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE)
  {
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

  if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, -1, -1, -1))
  {
    Serial.println("[SD] Pin configuration failed");
    SD_Card_Mounted = false;
    return false;
  }

  SD_D3_Enable();

  // 1-bit mode: mount point "/sdcard", mode1bit=true, formatOnFail=true
  if (!SD_MMC.begin("/sdcard", true, true))
  {
    Serial.println("[SD] Mount failed");
    SD_Card_Mounted = false;
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("[SD] No card detected");
    SD_Card_Mounted = false;
    return false;
  }

  SD_Card_Mounted = true;

  const char *typeStr = "UNKNOWN";
  if (cardType == CARD_MMC)
    typeStr = "MMC";
  if (cardType == CARD_SD)
    typeStr = "SDSC";
  if (cardType == CARD_SDHC)
    typeStr = "SDHC";

  uint64_t total = SD_MMC.totalBytes();
  uint64_t used = SD_MMC.usedBytes();
  SD_Card_SizeMB = (uint16_t)(total / (1024ULL * 1024ULL));

  Serial.printf("[SD] Card: %s  Total: %lluMB  Used: %lluMB  Free: %lluMB\n",
                typeStr,
                total / (1024ULL * 1024ULL),
                used / (1024ULL * 1024ULL),
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
  if (!SD_Card_Mounted && !remount)
    return false;

  // Probe does full SDMMC host teardown + re-init (~100ms)
  // This is the only reliable way to detect card presence on ESP32
  bool present = SD_CardProbe();

  if (!present && !remount)
  {
    Serial.println("[SD] Card not detected");
  }
  // If remount=true, SD_CardProbe already attempted the remount.
  // If it still failed, the card truly isn't there.
  if (!present && remount)
  {
    Serial.println("[SD] Card not detected (remount failed)");
  }
  if (present && remount && !SD_Card_Mounted)
  {
    // This shouldn't happen since CardProbe sets the flag, but just in case
    SD_Card_Mounted = true;
  }

  return present;
}

uint64_t SD_TotalBytes(void) { return SD_Card_Mounted ? SD_MMC.totalBytes() : 0; }
uint64_t SD_UsedBytes(void) { return SD_Card_Mounted ? SD_MMC.usedBytes() : 0; }
uint64_t SD_FreeBytes(void) { return SD_TotalBytes() - SD_UsedBytes(); }

uint16_t SD_GetFlashSizeMB(void)
{
  Flash_SizeMB = (uint16_t)(ESP.getFlashChipSize() / (1024 * 1024));
  return Flash_SizeMB;
}

// ═══════════════════════════════════════════════════════════════════
//  FILE & DIRECTORY OPERATIONS
// ═══════════════════════════════════════════════════════════════════

bool SD_Exists(const char *path)
{
  if (!SD_Card_Mounted)
    return false;
  return SD_MMC.exists(path);
}

size_t SD_FileSize(const char *path)
{
  if (!SD_Card_Mounted)
    return 0;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f)
    return 0;
  size_t s = f.size();
  f.close();
  return s;
}

bool SD_DeleteFile(const char *path)
{
  if (!SD_Card_Mounted)
    return false;
  return SD_MMC.remove(path);
}

bool SD_RenameFile(const char *from, const char *to)
{
  if (!SD_Card_Mounted)
    return false;
  return SD_MMC.rename(from, to);
}

bool SD_CreateDir(const char *path)
{
  if (!SD_Card_Mounted)
    return false;
  return SD_MMC.mkdir(path);
}

bool SD_RemoveDir(const char *path)
{
  if (!SD_Card_Mounted)
    return false;
  return SD_MMC.rmdir(path);
}

bool SD_FindFile(const char *directory, const char *fileName)
{
  if (!SD_Card_Mounted)
    return false;

  File dir = SD_MMC.open(directory);
  if (!dir || !dir.isDirectory())
  {
    Serial.printf("[SD] Directory not found: %s\n", directory);
    return false;
  }

  File entry = dir.openNextFile();
  while (entry)
  {
    if (strcmp(entry.name(), fileName) == 0)
    {
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

uint16_t SD_ListFiles(const char *directory, const char *extension,
                      char fileNames[][100], uint16_t maxFiles)
{
  if (!SD_Card_Mounted)
    return 0;

  File dir = SD_MMC.open(directory);
  if (!dir || !dir.isDirectory())
  {
    Serial.printf("[SD] Directory not found: %s\n", directory);
    return 0;
  }

  uint16_t count = 0;
  File entry = dir.openNextFile();

  while (entry && count < maxFiles)
  {
    if (!entry.isDirectory())
    {
      if (ext_match(entry.name(), extension))
      {
        strncpy(fileNames[count], entry.name(), 99);
        fileNames[count][99] = '\0';
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry)
    entry.close();
  dir.close();
  return count;
}

// ═══════════════════════════════════════════════════════════════════
//  TEXT FILE I/O
// ═══════════════════════════════════════════════════════════════════

size_t SD_ReadTextFile(const char *path, char *buffer, size_t bufferSize)
{
  if (!SD_Card_Mounted || !buffer || bufferSize == 0)
    return 0;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f)
  {
    Serial.printf("[SD] Cannot open: %s\n", path);
    return 0;
  }

  size_t toRead = f.size();
  if (toRead >= bufferSize)
    toRead = bufferSize - 1; // leave room for null

  size_t bytesRead = f.read((uint8_t *)buffer, toRead);
  buffer[bytesRead] = '\0';
  f.close();
  return bytesRead;
}

char *SD_ReadTextFileAlloc(const char *path, size_t *outSize)
{
  if (outSize)
    *outSize = 0;
  if (!SD_Card_Mounted)
    return nullptr;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f)
  {
    Serial.printf("[SD] Cannot open: %s\n", path);
    return nullptr;
  }

  size_t fileSize = f.size();
  // Allocate in PSRAM if available, else internal RAM
  char *buf = (char *)heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf)
  {
    buf = (char *)malloc(fileSize + 1);
  }
  if (!buf)
  {
    Serial.printf("[SD] Alloc failed for %u bytes\n", (unsigned)(fileSize + 1));
    f.close();
    return nullptr;
  }

  size_t bytesRead = f.read((uint8_t *)buf, fileSize);
  buf[bytesRead] = '\0';
  f.close();

  if (outSize)
    *outSize = bytesRead;
  return buf;
}

bool SD_WriteTextFile(const char *path, const char *content)
{
  if (!SD_Card_Mounted || !content)
    return false;

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f)
  {
    Serial.printf("[SD] Cannot create: %s\n", path);
    return false;
  }

  size_t written = f.print(content);
  f.close();
  return (written > 0);
}

bool SD_AppendTextFile(const char *path, const char *content)
{
  if (!SD_Card_Mounted || !content)
    return false;

  File f = SD_MMC.open(path, FILE_APPEND);
  if (!f)
  {
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

size_t SD_ReadBinaryFile(const char *path, uint8_t *buffer, size_t bufferSize)
{
  if (!SD_Card_Mounted || !buffer || bufferSize == 0)
    return 0;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f)
    return 0;

  size_t toRead = f.size();
  if (toRead > bufferSize)
    toRead = bufferSize;

  size_t bytesRead = f.read(buffer, toRead);
  f.close();
  return bytesRead;
}

bool SD_WriteBinaryFile(const char *path, const uint8_t *data, size_t length)
{
  if (!SD_Card_Mounted || !data)
    return false;

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f)
    return false;

  size_t written = f.write(data, length);
  f.close();
  return (written == length);
}

File SD_OpenFile(const char *path, const char *mode)
{
  if (!SD_Card_Mounted)
    return File();
  return SD_MMC.open(path, mode);
}

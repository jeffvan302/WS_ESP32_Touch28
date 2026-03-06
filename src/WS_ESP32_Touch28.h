#include "SD_Card.h"
#include "Display_ST7789.h"
#include "Touch_Gestures.h"
#include "BAT_Driver.h"
#include "Audio_PCM5101.h"
#include "PWR_Key.h"
#include "I2C_Driver.h"
#include "Gyro_QMI8658.h"
#include "LVGL_Driver.h"
#include "RTC_PCF85063.h"

inline void WS_ESP32_Init(void){
  Backlight_Init();
  LCD_Init();           // also initializes touch via Touch_Init()
  BAT_Init();
  PWR_Init();
  I2C_Init();
  PCF85063_Init();
  PCF85063_Loop();
  // Push an initial black screen
  LCD_FB_Start();
  LCD_FB_Clear(0x0000);
  LCD_FB_End();
  LCD_Update();

  SD_Init();

  Audio_Init();

  QMI8658_Init();    // uses Wire bus 0, already initialized by I2C_Init() above

  #if HAS_LVGL
     LVGL_Init();
  #endif
  
}

inline void WS_ESP32_Loop(void){
  Touch_Gesture_Process();
  PWR_Loop();
  QMI8658_Loop();
  #if HAS_LVGL
    LVGL_Loop();
  #endif
  LCD_Update();
  PCF85063_Loop();
  BAT_store_volt_history();
}
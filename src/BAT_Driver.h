#pragma once
#include <Arduino.h> 

#define BAT_ADC_PIN   8
#define Measurement_offset 0.990476   

extern float BAT_analogVolts;


void BAT_Init(void);
float BAT_Get_Volts(void);
float BAT_voltageToPercent(float v);
void BAT_store_volt_history(void);
float BAT_read_avarage_Volts(void);
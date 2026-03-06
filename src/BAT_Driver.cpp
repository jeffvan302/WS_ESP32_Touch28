#include "BAT_Driver.h"

float BAT_analogVolts = 0;
static const int volt_history_N = 20;
static float volts_hist[volt_history_N];
static int volt_pos = 0;
static int volt_count = 0;

void BAT_store_volt_history(void){
  if (volt_pos >= volt_history_N) volt_pos = 0;
  volts_hist[volt_pos] = BAT_Get_Volts();
  volt_count++;
  volt_pos++;
  if (volt_count > volt_history_N) volt_count = volt_history_N;
}

float BAT_read_avarage_Volts(void){
  float total = 0.0;
  if (volt_count == 0) return 0.0;
  for (int pos = 0; pos < volt_history_N; pos++){
    total += volts_hist[pos];
  }
  total = total / (float)volt_count;
  return total;
}

void BAT_Init(void)
{
  //set the resolution to 12 bits (0-4095)
  analogReadResolution(12);
}

float BAT_Get_Volts(void)
{
  int Volts = analogReadMilliVolts(BAT_ADC_PIN); // millivolts
  BAT_analogVolts = (float)(Volts * 3.0 / 1000.0) / Measurement_offset;
  // printf("BAT voltage : %.2f V\r\n", BAT_analogVolts);
  return BAT_analogVolts;
}

// Convert Li-ion battery voltage to percentage using piecewise linear interpolation.
// Input: voltage (float, in volts). Output: percent (0.0 to 100.0).
float BAT_voltageToPercent(float v)
{
  // Lookup table (percent, voltage). Voltages are in descending order.
  static const int   N = 11;
  static const float volts[N]   = {4.1, 4.05, 3.98, 3.92, 3.87, 3.82, 3.79, 3.77, 3.74, 3.68, 3.40};
  static const float pct[N]     = {100,  90,   80,   70,   60,   50,   40,   30,   20,   10,   0   };

  // Clamp above/below table range
  if (v >= volts[0]) return pct[0];
  if (v <= volts[N - 1]) return pct[N - 1];

  // Find the segment v falls into and interpolate linearly
  for (int i = 0; i < N - 1; i++)
  {
    float vHigh = volts[i];
    float vLow  = volts[i + 1];

    if (v <= vHigh && v >= vLow)
    {
      float pHigh = pct[i];
      float pLow  = pct[i + 1];

      // Linear interpolation:
      // t = (v - vLow) / (vHigh - vLow), then p = pLow + t*(pHigh - pLow)
      float t = (v - vLow) / (vHigh - vLow);
      return pLow + t * (pHigh - pLow);
    }
  }

  // Shouldn't get here, but return 0% as a safe fallback.
  return 0.0f;
}
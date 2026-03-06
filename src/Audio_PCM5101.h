#pragma once
#include "Arduino.h"
#include "Audio.h"
#include "SD_Card.h"
// Digital I/O used
#define I2S_DOUT      47
#define I2S_BCLK      48  
#define I2S_LRC       38      // I2S_WS

#define Volume_MAX            21
#define AUDIO_TASK_PRIORITY   19    // just below LCD task (20)
#define AUDIO_TASK_CORE        0    // same core as LCD task


extern Audio audio;
extern uint8_t Volume;

void Play_Music_test();
void Audio_Loop();

void Audio_Init();
void Volume_adjustment(uint8_t Volume);
void Play_Music(const char* directory, const char* fileName);
void Music_pause(); 
void Music_resume();    
uint32_t Music_Duration();  
uint32_t Music_Elapsed();   
uint16_t Music_Energy();    

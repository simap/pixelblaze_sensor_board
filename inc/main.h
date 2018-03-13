#ifndef _MAIN_H_
#define _MAIN_H_

#include "stm32f0xx.h"
#include "stdbool.h"
#include "string.h"
#include "stdlib.h"

#define HIGH_N 512
#define HIGH_NLOG2 9

#define LOW_N 32
#define LOW_NLOG2 5

#define LIS3DH_ADDR (0x18<<1)


extern const short Sinewave[];
extern int fix_fft(short fr[], short fi[], short m, short inverse);
extern int32_t fix16_sqrt(int32_t inValue);

void initRcc();
void initTim1();
void initAdc();
void initGpio();
void initUart();
void initDma();
void initI2C();

void writeToUsart(uint8_t * outBuffer, uint32_t len);
void i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value);
void i2cReadReg(uint8_t addr, uint8_t reg, uint8_t * value, uint8_t len);
void initAccelerometer();
void startAccelerometerPoll();

void processSensorData(int16_t * audioBuffer, int16_t * audio400HzBuffer, volatile uint16_t adcBuffer[7], volatile int16_t accelerometer[3]);


#endif

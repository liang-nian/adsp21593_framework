#ifndef ADAU1962_H_
#define ADAU1962_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Adau19620Init(void);

/* 1 after PLL_LOCK (PLL_CLK_CTRL1 bit 2) is seen. A2B discovery needs this. */
uint8_t Adau1962ClkReady(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADAU1962_H_

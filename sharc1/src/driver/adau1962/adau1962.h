/*
 * ADAU1962, I2C address 0x04. The only BCLK/FS master on this board.
 * TDM16, 48 kHz, sample on the falling edge. Init writes the config table and waits for PLL lock.
 */

#ifndef ADAU1962_H_
#define ADAU1962_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Adau19620Init(void);

#ifdef __cplusplus
}
#endif

#endif

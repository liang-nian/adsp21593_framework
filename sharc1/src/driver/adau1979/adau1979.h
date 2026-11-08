/*
 * ADAU1979, I2C address 0x11. TDM16 slave, 48 kHz, data launched on the rising BCLK edge.
 * SAI_CTRL0 is 0x22.
 */

#ifndef ADAU1979_H_
#define ADAU1979_H_

#ifdef __cplusplus
extern "C" {
#endif

void Adau19790Init(void);

#ifdef __cplusplus
}
#endif

#endif

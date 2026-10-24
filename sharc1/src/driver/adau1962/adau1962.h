#ifndef ADAU1962_H_
#define ADAU1962_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 0: PLL locked and the register table was written. 1: TWI or PLL failed. */
uint8_t Adau19620Init(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADAU1962_H_

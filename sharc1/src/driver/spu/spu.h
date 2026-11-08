/*
 * Opens peripheral access for SPORT0/1/2, UART0, and UART0 DMA.
 * Runs before anything that uses those masters.
 */

#ifndef SPU_H_
#define SPU_H_

#ifdef __cplusplus
extern "C" {
#endif

void Spu0Init(void);

#ifdef __cplusplus
}
#endif

#endif

/*
 * TMR0. Programmed for 1 ms from a 125 MHz SCLK. The tick calls Uart0Poll.
 */

#ifndef TIMER_H_
#define TIMER_H_

#ifdef __cplusplus
extern "C" {
#endif

void Tmr0Init(void);

void Tmr0En(uint8_t is_en);

void Tmr0Close(void);

#ifdef __cplusplus
}
#endif

#endif

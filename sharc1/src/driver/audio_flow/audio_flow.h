/*
 * Audio path bring-up. Order is SPU, UART, TMR, switch, SRU, DAC, ADC, A2B, SPORT,
 * then the core2 monitor flag is cleared. UART RX DMA starts before TMR0,
 * because the 1 ms tick calls Uart0Poll.
 */

#ifndef AUDIO_FLOW_H_
#define AUDIO_FLOW_H_

#ifdef __cplusplus
extern "C" {
#endif

void AudioFlowInit(void);

#ifdef __cplusplus
}
#endif

#endif

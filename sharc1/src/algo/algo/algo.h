/*
 * Core1 algorithm. ADC channels 0-3 pass through to the DAC. Channels 4-7 carry a 1 kHz tone.
 */

#ifndef ALGO_ALGO_H_
#define ALGO_ALGO_H_

#ifdef __cplusplus
extern "C" {
#endif

void AudioProcess(void);

#ifdef __cplusplus
}
#endif

#endif

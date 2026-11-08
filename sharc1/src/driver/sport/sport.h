/*
 * Six half-SPORTs. Clock and frame sync come from off-chip. DMA buffers sit in L1
 * and convert to and from the float adc, dac, and a2b buffers.
 * When all six finish, the audio callback hands a2b0's first 4 channels to core2,
 * runs this core's AudioProcess first, then waits and places core2's result on DAC channels 8-11.
 * This core's load covers only AudioProcess. The whole route is counted separately.
 */

#ifndef SPORT_H_
#define SPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_LEN   (48u)
#define TDM_NORM    (16u)
#define TDM_A2B     (2u * TDM_NORM)

float* adc_in(void);
float* dac_out(void);
float* a2b0_in(void);
float* a2b0_out(void);
float* a2b1_in(void);
float* a2b1_out(void);
void CpuMsgMon(void);
void CpuMsgMonReq(void);
void SpInit(void);

#ifdef __cplusplus
}
#endif

#endif

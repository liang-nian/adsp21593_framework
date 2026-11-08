/*
 * Core2 audio entry. The software interrupt copies shared data into L1 audio_in,
 * calls AudioProcess, copies audio_out back to shared memory, and writes this core's load.
 * IN_CH and OUT_CH describe only the stream core1 passes in.
 */

#ifndef AUDIO_FLOW_H_
#define AUDIO_FLOW_H_

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_LEN (48u)
#define IN_CH     (4u)
#define OUT_CH    (4u)

void AudioFlowInit(void);
float* audio_in(void);
float* audio_out(void);

#ifdef __cplusplus
}
#endif

#endif

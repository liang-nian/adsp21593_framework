#include <string.h>

#include "algo.h"
#include "audio_flow/audio_flow.h"

void AudioProcess(void) {
  memcpy(audio_out(), audio_in(), sizeof(float) * IN_CH * FRAME_LEN);
};

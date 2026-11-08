#include <math.h>
#include <stdint.h>

#include "algo.h"
#include "sport/sport.h"

static float Harm1000(void) {
  static float ft = 0.f;
  ft += 1000.f / 48000.f;
  ft = ft - floorf(ft);
  return sinf(2 * 3.14159265358979323846264338327950288419716939937510f * ft);
};

void AudioProcess(void) {
  uint32_t i;
  float sin_1000;
  float* dac_out_ptr = dac_out();
  float* adc_in_ptr = adc_in();
  for (i = 0; i < FRAME_LEN; ++i) {
    sin_1000 = Harm1000();
    dac_out_ptr[i] = adc_in_ptr[i];
    dac_out_ptr[FRAME_LEN + i] = adc_in_ptr[FRAME_LEN + i];
    dac_out_ptr[2 * FRAME_LEN + i] = adc_in_ptr[2 * FRAME_LEN + i];
    dac_out_ptr[3 * FRAME_LEN + i] = adc_in_ptr[3 * FRAME_LEN + i];
    dac_out_ptr[4 * FRAME_LEN + i] = sin_1000;
    dac_out_ptr[5 * FRAME_LEN + i] = sin_1000;
    dac_out_ptr[6 * FRAME_LEN + i] = sin_1000;
    dac_out_ptr[7 * FRAME_LEN + i] = sin_1000;
  }
};

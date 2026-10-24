#include <stdint.h>
#include <math.h>

#include "audio_flow.h"
#include "spu/spu.h"
#include "ad2428/ad2428.h"
#include "adau1962/adau1962.h"
#include "adau1979/adau1979.h"
#include "sport/sport.h"
#include "sru/signal_route.h"
#include "switch/switch.h"
#include "twi/twi.h"
#include "uart/uart.h"
#include "timer/timer.h"

float Harm1000(void) {
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
  float* a2b0_in_ptr = a2b0_in();
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
    dac_out_ptr[8 * FRAME_LEN + i] = a2b0_in_ptr[i];
    dac_out_ptr[9 * FRAME_LEN + i] = a2b0_in_ptr[2 * FRAME_LEN + i];
    dac_out_ptr[10 * FRAME_LEN + i] = a2b0_in_ptr[FRAME_LEN + i];
    dac_out_ptr[11 * FRAME_LEN + i] = a2b0_in_ptr[3 * FRAME_LEN + i];
  }
};

void AudioFlowInit(void) {
  /* SPU before any UART/SPORT DMA master. UART RX DMA before TMR0, because
   * the 1 ms tick calls Uart0Poll. SRU then the ADAU1962 clock master, then
   * the ADC slave. Ad24280Init refuses to run until that PLL is locked and
   * TWI2 is idle (USBi must not be plugged in). SPORT DMA starts last. */
  Spu0Init();
  Uart0Init();
  Tmr0Init();
  Sw0Init();
  SruInit();
  Adau19620Init();
  Adau19790Init();
  Ad24280Init();
  // Ad24281Init();
  SpInit();
};

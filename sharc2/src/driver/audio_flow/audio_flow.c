#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include <services/int/adi_int.h>

#include "audio_flow.h"
#include "share/share.h"
#include "algo/algo.h"

#define EMA_K (0.001f)

#pragma default_section(ALLDATA, "seg_l1_block1")
static float audio_in_[IN_CH * FRAME_LEN];
static float audio_out_[OUT_CH * FRAME_LEN];
#pragma default_section(ALLDATA)

static float cpu_load_;
static float max_cpu_load_;
static float ave_cpu_load_;

float* audio_in(void) {
  return audio_in_;
};

float* audio_out(void) {
  return audio_out_;
};

static void AudioCallBack(uint32_t iid) {
  uint64_t cnt;

  cnt = __builtin_emuclk();
  memcpy(audio_in_, Core2TransData(), sizeof(float) * IN_CH * FRAME_LEN);
  AudioProcess();
  memcpy(Core2TransData(), audio_out_, sizeof(float) * OUT_CH * FRAME_LEN);
  cnt = __builtin_emuclk() - cnt;
  cpu_load_ = cnt / 1000.f;
  max_cpu_load_ = fmaxf(cpu_load_, max_cpu_load_);
  ave_cpu_load_ = EMA_K * cpu_load_ + (1.f - EMA_K) * ave_cpu_load_;
  Core2TransMon(cpu_load_, max_cpu_load_, ave_cpu_load_);
  Core2TransFlagSet(1u);
};

void AudioFlowInit(void) {
  adi_int_InstallHandler((uint32_t)INTR_SYS_SOFT0_INT,
                         (ADI_INT_HANDLER_PTR)AudioCallBack, NULL, true);
};

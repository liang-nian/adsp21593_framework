#include <stdint.h>

#include "share.h"
#include "audio_flow/audio_flow.h"

#define CORE2_TRANS_CH (32u)
#define CORE2_TRANS_ADDR (0x2001c000u)
#define CORE2_TRANS_BYTES (0x4000u)

typedef struct Core2Trans {
  uint32_t flag;
  float cpu_load_;
  float max_cpu_load_;
  float ave_cpu_load_;
  float data_[CORE2_TRANS_CH * FRAME_LEN];
  uint8_t mon_flag;
} Core2Trans;

#define core2_trans ((volatile Core2Trans*)CORE2_TRANS_ADDR)

typedef char Core2TransFits[(sizeof(Core2Trans) <= CORE2_TRANS_BYTES) ? 1 : -1];

void Core2TransFlagSet(uint32_t flag) {
  core2_trans->flag = flag;
};

float* Core2TransData(void) {
  return (float*)core2_trans->data_;
};

void Core2TransMon(float load, float max_load, float ave_load) {
  core2_trans->cpu_load_ = load;
  core2_trans->max_cpu_load_ = max_load;
  core2_trans->ave_cpu_load_ = ave_load;
};

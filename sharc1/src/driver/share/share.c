#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "share.h"
#include "sport/sport.h"
#include "link/link.h"

#define CORE2_TRANS_CH ((TDM_A2B > TDM_NORM) ? TDM_A2B : TDM_NORM)
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

uint32_t Core2TransFlag(void) {
  return core2_trans->flag;
};

float* Core2TransData(void) {
  return (float*)core2_trans->data_;
};

void Core2TransMonFlagSet(uint8_t flag) {
  core2_trans->mon_flag = flag;
};

static void Core2CpuMonTx(const char* format, ...) {
  char text[48];
  int len;
  va_list args;

  va_start(args, format);
  len = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (len <= 0) {
    return;
  }
  if ((size_t)len >= sizeof(text)) {
    len = (int)sizeof(text) - 1;
  }
  LinkTx(LINK_CMD_CORE2_CPU_MON, (uint8_t*)text, (uint32_t)len);
};

void Core2CpuMonReq(void) {
  core2_trans->mon_flag = 1u;
};

void Core2CpuMon(void) {
  if (!core2_trans->mon_flag) {
    return;
  }
  core2_trans->mon_flag = 0u;
  Core2CpuMonTx("core2 cpu load: %.3f.\n", core2_trans->cpu_load_);
  Core2CpuMonTx("core2 max cpu load: %.3f.\n", core2_trans->max_cpu_load_);
  Core2CpuMonTx("core2 ave cpu load: %.3f.\n", core2_trans->ave_cpu_load_);
};

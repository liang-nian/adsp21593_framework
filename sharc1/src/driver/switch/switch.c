#include <stdint.h>

#include "switch.h"
#include "switch_reg.h"
#include "twi/twi.h"

/* ADAU1962A Rev. A needs 300 ms after PU/RST. One iteration is at least
 * one core cycle, so at 1 GHz this count is at least 300 ms. */
#define TIME_BND    (300000000u)

static void SwCfg(uint8_t dev, SwReg* cfg, uint32_t len) {
  uint32_t i;
  uint32_t delay = TIME_BND;

  Twi2DevAddrSet(dev);
  for (i = 0; i < len; ++i) {
    Twi2RegShtWr(cfg[i].reg, cfg[i].val);
  }
  while(delay--){
    asm("nop;");
  }
};

void Sw0Init(void) {
  Twi2Open();
  SwCfg(SW0_DEV, sw0_en, SW0_EN_LEN);
  SwCfg(SW0_DEV, sw0_rst, SW0_RST_LEN);
  Twi2Stop();
};

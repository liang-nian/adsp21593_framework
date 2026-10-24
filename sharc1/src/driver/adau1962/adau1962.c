#include <stdint.h>

#include "adau1962_reg.h"
#include "adau1962.h"
#include "twi/twi.h"
#include "log/log.h"

#define TIME_BND    (0xFFFFu)

static void Adau1962Cfg(uint8_t dev, Adau1962Reg* cfg, uint32_t len) {
  uint32_t i;
  uint8_t val;

  Twi2DevAddrSet(dev);
  for (i = 0; i < len; ++i) {
    Twi2RegShtWr(cfg[i].reg, cfg[i].val);
    val = Twi2RegShtRd(cfg[i].reg);
    if (val != cfg[i].val) {
      ShtLog("adau1962 cfg fail.\n");
    }
  }
};

static uint8_t Adau1962Pll(uint8_t dev, Adau1962Reg* pll, uint32_t len) {
  uint32_t delay, i;
  uint8_t val;

  Twi2DevAddrSet(dev);
  for (i = 0; i < len; ++i) {
    delay = TIME_BND;
    Twi2RegShtWr(pll[i].reg, pll[i].val);
    while (delay--) {
      asm("nop;");
    }
  }
  delay = 0u;
  val = Twi2RegShtRd(ADAU1962_PLL_CTL_CTRL1);
  /* PLL_CLK_CTRL1 bit 2 is PLL_LOCK (ADAU1962A datasheet, read-only). */
  while(!((val & 0x4) >> 2)){
    if (delay > TIME_BND) {
      ShtLog("adau1962 pll fail.\n");
      return 1u;
    }
    val = Twi2RegShtRd(ADAU1962_PLL_CTL_CTRL1);
    delay++;
    asm("nop;");
  }
  return 0u;
};

uint8_t Adau19620Init(void) {
  uint8_t pll_fail;

  if (Twi2Open() != 0u) {
    return 1u;
  }
  pll_fail = Adau1962Pll(ADAU19620_DEV, adau19620_pll, ADAU19620_PLL_LEN);
  if (pll_fail == 0u) {
    Adau1962Cfg(ADAU19620_DEV, adau19620_cfg, ADAU19620_CFG_LEN);
  }
  Twi2Stop();
  return pll_fail;
};

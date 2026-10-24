#include <stdint.h>

#include "switch.h"
#include "switch_reg.h"
#include "twi/twi.h"

#define TIME_BND    (0xFFFFu)

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

/* ADAU1962A Rev.A: after PU/RST rises the part needs 300 ms before it is
 * guaranteed stable. 300e6 core cycles is 300 ms at the 1 GHz CCLK maximum
 * and longer at any slower core clock, so the wait never undershoots. */
static void SwResetSettle(void) {
  const uint64_t kSettleCycles = 300000000ull;
  uint64_t start = __builtin_emuclk();

  while ((__builtin_emuclk() - start) < kSettleCycles) {
    asm("nop;");
  }
};

void Sw0Init(void) {
  Twi2Open();
  SwCfg(SW0_DEV, sw0_en, SW0_EN_LEN);
  SwCfg(SW0_DEV, sw0_rst, SW0_RST_LEN);
  Twi2Stop();
  SwResetSettle();
};

#include <sru.h>

#include "signal_route.h"

void SruInit(void) {
  // Enable all dai.
  *pREG_PADS0_DAI0_IE=0xFFFFF;
  *pREG_PADS0_DAI1_IE=0xFFFFF;

  // Dac bclk src dai1 pin5.
  SRU(LOW, DAI1_PBEN05_I);
  // Bclk to sp.
  SRU(DAI1_PB05_O, SPT0_ACLK_I);
  SRU(DAI1_PB05_O, SPT0_BCLK_I);
  SRU(DAI1_PB05_O, SPT1_ACLK_I);
  SRU(DAI1_PB05_O, SPT1_BCLK_I);
  SRU(DAI1_PB05_O, SPT2_ACLK_I);
  SRU(DAI1_PB05_O, SPT2_BCLK_I);
  // Bclk to adc dai1 pin12.
  SRU(HIGH, DAI1_PBEN12_I);
  SRU(DAI1_PB05_O, DAI1_PB12_I);
  // Bclk to a2b0 dai0 pin3.
  SRU(HIGH, DAI0_PBEN03_I);
  SRU(DAI1_PB05_O, DAI0_PB03_I);
  // Bclk to a2b1 dai1 pin8.
  SRU(HIGH, DAI1_PBEN08_I);
  SRU(DAI1_PB05_O, DAI1_PB08_I);

  // Fs src dac dai1 pin4.
  SRU(LOW, DAI1_PBEN04_I);
  // Fs to sp.
  SRU(DAI1_PB04_O, SPT0_AFS_I);
  SRU(DAI1_PB04_O, SPT0_BFS_I);
  SRU(DAI1_PB04_O, SPT1_AFS_I);
  SRU(DAI1_PB04_O, SPT1_BFS_I);
  SRU(DAI1_PB04_O, SPT2_AFS_I);
  SRU(DAI1_PB04_O, SPT2_BFS_I);
  // Fs to adc dai1 pin20.
  SRU(HIGH,DAI1_PBEN20_I);
  SRU(DAI1_PB04_O,DAI1_PB20_I);
  // Fs to a2b0 dai0 pin4.
  SRU(HIGH, DAI0_PBEN04_I);
  SRU(DAI1_PB04_O, DAI0_PB04_I);
  // Fs to a2b1 dai1 pin9.
  SRU(HIGH, DAI1_PBEN09_I);
  SRU(DAI1_PB04_O, DAI1_PB09_I);

  // Sp0a data to dac dai1 pin1.
  SRU(HIGH,DAI1_PBEN01_I);
  SRU(SPT0_AD0_O,DAI1_PB01_I);
  // Sp0b data from adc dai1 pin6.
  SRU(LOW,DAI1_PBEN06_I);
  SRU(DAI1_PB06_O,SPT0_BD0_I);
  // Sp1a data to a2b0 dai0 pin5 and dai0 pin6.
  SRU(HIGH, DAI0_PBEN05_I);
  SRU(SPT1_AD0_O, DAI0_PB05_I);
  SRU(HIGH, DAI0_PBEN06_I);
  SRU(SPT1_AD1_O, DAI0_PB06_I);
  // Sp1b data from a2b0 dai0 pin1 and dai0 pin2.
  SRU(LOW, DAI0_PBEN01_I);
  SRU(DAI0_PB01_O, SPT1_BD0_I);
  SRU(LOW, DAI0_PBEN02_I);
  SRU(DAI0_PB02_O, SPT1_BD1_I);
  // Sp2a data to a2b1 dai1 pin19 and dai1 pin10.
  SRU(HIGH, DAI1_PBEN19_I);
  SRU(SPT2_AD0_O, DAI1_PB19_I);
  SRU(HIGH, DAI1_PBEN10_I);
  SRU(SPT2_AD1_O, DAI1_PB10_I);
  // Sp2b data from a2b1 dai1 pin11 and dai1 pin7.
  SRU(LOW, DAI1_PBEN11_I);
  SRU(DAI1_PB11_O, SPT2_BD0_I);
  SRU(LOW, DAI1_PBEN07_I);
  SRU(DAI1_PB07_O, SPT2_BD1_I);
};

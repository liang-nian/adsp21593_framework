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
#include "share/share.h"

void AudioFlowInit(void) {

  Spu0Init();
  Uart0Init();
  Tmr0Init();
  Sw0Init();
  SruInit();
  Adau19620Init();
  Adau19790Init();
  Ad24280Init();
  SpInit();
  Core2TransMonFlagSet(0u);
};

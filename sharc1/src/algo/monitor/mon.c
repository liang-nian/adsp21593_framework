#include <stdint.h>

#include "uart/uart.h"
#include "ad2428/ad2428.h"
#include "sport/sport.h"
#include "mon.h"


// Operation must consider about repeat 100 times during 1 ms.
static void MonProcess(uint8_t type) {
  switch (type) {
    case 0xAAu:
      Ad24280Mon();
      Ad24281Mon();
      break;
    case 0xBBu:
      Uart0Mon();
      break;
    case 0xCCu:
      CpuMsgMon();
      break;
    default:
      break;
  }
};

void MonTmp(void) {
  uint8_t type = 0u;

  while (!Uart0Rd(&type)){
    MonProcess(type);
  }
};

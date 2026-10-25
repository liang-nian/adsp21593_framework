/*****************************************************************************
 * framework_adsp21593_Core1.c
 *****************************************************************************/

#include "sharc1.h"

#include "adi_initialize.h"
#include <sys/platform.h>
#include <sys/adi_core.h>
#include "audio_flow/audio_flow.h"
#include "ad2428/ad2428.h"
#include "sport/sport.h"
#include "log/log.h"
#include "tune/tune.h"
#include "uart/uart.h"

/** 
 * If you want to use command program arguments, then place them in the following string. 
 */
char __argv_string[] = "";

int main(int argc, char *argv[])
{
	/**
	 * Initialize managed drivers and/or services that have been added to 
	 * the project.
	 * @return zero on success 
	 */
	adi_initComponents();
	
	/**
	 * The default startup code does not include any functionality to allow
	 * core 1 to enable core 2. A convenient way to enable
	 * core 2 is to use the adi_core_enable function. 
	 */
	adi_core_enable(ADI_CORE_SHARC1);

	/* Begin adding your custom code here */

	AudioFlowInit();
	LngLog("I", "audio init done\n");

	/* 5A A5 01 plus Modbus CRC of the command byte. Same frame turns echo off. */
	static const uint8_t wb_off[] = {0x5Au, 0xA5u, 0x01u, 0x7Eu, 0x80u};
	uint8_t wb_on = 0u;
	uint8_t wb_idx = 0u;

	while (1) {
	  uint8_t byte;

	  if (wb_on) {
	    if (Uart0Rd(&byte)) {
	      continue;
	    }
	    if (byte == wb_off[wb_idx]) {
	      wb_idx++;
	      if (wb_idx == sizeof(wb_off)) {
	        wb_on = 0u;
	        wb_idx = 0u;
	      }
	      continue;
	    }
	    if (wb_idx > 0u) {
	      Uart0Wr((uint8_t*)wb_off, wb_idx);
	      wb_idx = 0u;
	      if (byte == wb_off[0]) {
	        wb_idx = 1u;
	        continue;
	      }
	    }
	    Uart0Wr(&byte, 1u);
	    continue;
	  }

	  while (!Uart0Rd(&byte)) {
	    TuneRx(byte);
	  }
	  if (TuneWbFlag()) {
	    wb_on = 1u;
	    wb_idx = 0u;
	    TuneWbFlagClr();
	  }
	  if (TuneA2bMonFlag()) {
	    Ad24280Mon();
	    TuneA2bMonFlagClr();
	  }
	  if (TuneUartMonFlag()) {
	    Uart0Mon();
	    TuneUartMonFlagClr();
	  }
	  if (TuneCpuMonFlag()) {
	    CpuMsgMon();
	    TuneCpuMonFlagClr();
	  }
	};

	return 0;
}


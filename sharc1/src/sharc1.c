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
#include "link/link.h"
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

	while (1) {
	  LinkRx();
	  Ad24280Mon();
	  Uart0Mon();
	  CpuMsgMon();
	};

	return 0;
}


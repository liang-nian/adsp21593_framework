/*****************************************************************************
 * framework_adsp21593_Core1.c
 *****************************************************************************/

#include "sharc1.h"

#include "adi_initialize.h"
#include <sys/platform.h>
#include <sys/adi_core.h>
#include "audio_flow/audio_flow.h"
#include "monitor/mon.h"

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

	while (1) {
	  MonTmp();
	};

	return 0;
}


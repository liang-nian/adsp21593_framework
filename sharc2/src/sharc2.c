/*****************************************************************************
 * framework_adsp21593_Core2.c
 *****************************************************************************/

#include "sharc2.h"

#include "adi_initialize.h"
#include <sys/platform.h>

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
	
	/* Begin adding your custom code here */
	while (1) {};

	return 0;
}


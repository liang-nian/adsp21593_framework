#include "sharc1.h"

#include "adi_initialize.h"
#include <sys/platform.h>
#include <sys/adi_core.h>
#include "audio_flow/audio_flow.h"
#include "ad2428/ad2428.h"
#include "sport/sport.h"
#include "share/share.h"
#include "log/log.h"
#include "link/link.h"
#include "uart/uart.h"

char __argv_string[] = "";

int main(int argc, char *argv[])
{

	adi_initComponents();

	adi_core_enable(ADI_CORE_SHARC1);

	AudioFlowInit();
	LngLog("I", "audio init done\n");

	while (1) {
	  LinkRx();
	  Ad24280Mon();
	  Uart0Mon();
	  CpuMsgMon();
	  Core2CpuMon();
	};

	return 0;
}

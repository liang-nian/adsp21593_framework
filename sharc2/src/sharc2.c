#include "sharc2.h"

#include "adi_initialize.h"
#include <sys/platform.h>
#include "audio_flow/audio_flow.h"

char __argv_string[] = "";

int main(int argc, char *argv[])
{

	adi_initComponents();

	AudioFlowInit();
	while (1) {};

	return 0;
}

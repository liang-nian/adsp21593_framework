#include "ad2428_reg.h"

/*
 * Second A2B (AD24281) config table — intentionally empty.
 *
 * The official Analog Devices ADZS-21593-EZKIT / SOM-21593 + SOMCRR-EZKIT
 * hardware provides only one A2B master path (AD24280 @ 0x68/0x69).
 * There is no second on-board A2B transceiver for AD24281.
 *
 * Keep this stub so the build still links Ad24281Init / Ad24281Mon without
 * shipping any product-specific SigmaStudio export.
 */

Ad2428Reg ad24281_cfg[AD24281_CFG_LEN] = {
	{0}
};

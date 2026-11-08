/*
 * Shared block core2_trans, in L2 that neither core caches, at 0x2001c000, 16KB.
 * This core writes the flag and the data to process, reads back core2's result and load,
 * and sends core2's load when its monitor is enabled. The layout matches core2's share.
 */

#ifndef SHARE_H_
#define SHARE_H_

#include <stdint.h>

void Core2TransFlagSet(uint32_t flag);
uint32_t Core2TransFlag(void);
float* Core2TransData(void);
void Core2TransMonFlagSet(uint8_t flag);
void Core2CpuMon(void);
void Core2CpuMonReq(void);

#endif

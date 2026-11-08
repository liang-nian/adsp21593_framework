/*
 * The same core2_trans block as core1. This core reads the data, writes back the result
 * and its own load, then sets the flag. The layout is defined in this file.
 */

#ifndef SHARE_H_
#define SHARE_H_

#include <stdint.h>

void Core2TransFlagSet(uint32_t flag);
float* Core2TransData(void);
void Core2TransMon(float load, float max_load, float ave_load);

#endif

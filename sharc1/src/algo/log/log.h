/*
 * UART log helpers.
 * ShtLog writes the formatted text through TuneTx, command 0x0C.
 * LngLog writes "file:line level message". File and line come from the call.
 */

#ifndef ALGO_LOG_LOG_H_
#define ALGO_LOG_LOG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ShtLog(const char* format, ...);
uint8_t LngLogAt(const char* file, int line, const char* level,
                 const char* format, ...);

/* level is a caller string such as "E". The format follows, then its args. */
#define LngLog(level, ...) LngLogAt(__FILE__, __LINE__, (level), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ALGO_LOG_LOG_H_ */

/*
 * Logs go out on the link, command 0x0C. ShtLog sends formatted text.
 * LngLog adds the file name, line, and level.
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

#define LngLog(level, ...) LngLogAt(__FILE__, __LINE__, (level), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif

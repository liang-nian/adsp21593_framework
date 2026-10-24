#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#include "log.h"
#include "uart/uart.h"

static char msg[128];

uint8_t ShtLog(const char* format, ...) {
  va_list args;
  int n;

  va_start(args, format);
  n = vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);

  /* vsnprintf returns the untruncated length. n == sizeof(msg) does not fit. */
  if (n < 0 || (uint32_t)n >= sizeof(msg)) {
    return 1u;
  }
  return Uart0Wr((uint8_t*)msg, (uint32_t)n);
};

uint8_t LngLog(const char* level, const char* format, ...) {
  (void)level;
  (void)format;
  return 0u;
};

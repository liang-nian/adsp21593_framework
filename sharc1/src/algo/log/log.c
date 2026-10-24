#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "uart/uart.h"

static char msg[128];

uint8_t ShtLog(const char* format, ...) {
  int len;
  va_list args;

  va_start(args, format);
  len = vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);

  /* vsnprintf returns the length that would have been written, excluding
   * the NUL. A negative return is an error. A return >= sizeof(msg) means
   * the buffer was truncated; sending that count reads past msg. */
  if (len <= 0) {
    return 0u;
  }
  if ((size_t)len >= sizeof(msg)) {
    len = (int)sizeof(msg) - 1;
  }
  return Uart0Wr((uint8_t*)msg, (uint32_t)len);
};

uint8_t LngLog(const char* level, const char* format, ...) {

  return 0u;
};

#include "uart/uart.h"

static char msg[128];

uint8_t ShtLog(const char* format, ...) {
  uint32_t len = 0u;

  va_list args;
  va_start(args, format);
  len = vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);

  if (len > 128u) return 0u;

  return Uart0Wr((uint8_t*)msg, len);
};

uint8_t LngLog(const char* level, const char* format, ...) {

  return 0u;
};

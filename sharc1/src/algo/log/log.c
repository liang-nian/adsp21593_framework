#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "log/log.h"
#include "tune/tune.h"

static char msg[128];

/* vsnprintf returns the length that would have been written, excluding
 * the NUL. A negative return is an error. A return >= sizeof(msg) means
 * the buffer was truncated; sending that count reads past msg. */
static uint8_t SendMsg(int len) {
  if (len <= 0) {
    return 0u;
  }
  if ((size_t)len >= sizeof(msg)) {
    len = (int)sizeof(msg) - 1;
  }
  return TuneTx(TUNE_CMD_LOG, (uint8_t*)msg, (uint32_t)len);
};

uint8_t ShtLog(const char* format, ...) {
  int len;
  va_list args;

  va_start(args, format);
  len = vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);
  return SendMsg(len);
};

/* __FILE__ is often a full path. Keep the last component so the 128-byte
 * buffer still has room for the level and the message. */
static const char* FileName(const char* path) {
  const char* name;
  const char* p;

  if (path == NULL) {
    return "";
  }
  name = path;
  for (p = path; *p != '\0'; ++p) {
    if ((*p == '/') || (*p == '\\')) {
      name = p + 1;
    }
  }
  return name;
};

uint8_t LngLogAt(const char* file, int line, const char* level,
                 const char* format, ...) {
  int prefix;
  int body;
  va_list args;

  if (format == NULL) {
    return 0u;
  }
  if (level == NULL) {
    level = "";
  }
  prefix = snprintf(msg, sizeof(msg), "%s:%d %s ", FileName(file), line, level);
  if (prefix < 0) {
    return 0u;
  }
  if ((size_t)prefix >= sizeof(msg)) {
    return SendMsg(prefix);
  }
  va_start(args, format);
  body = vsnprintf(msg + prefix, sizeof(msg) - (size_t)prefix, format, args);
  va_end(args);
  if (body < 0) {
    return SendMsg(prefix);
  }
  return SendMsg(prefix + body);
};

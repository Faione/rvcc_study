#include "rvcc.h"

// 格式化字符串
char *format(char *fmt, ...) {
  char *buf;
  size_t len;

  // 申请堆上内存并将起始地址保存到 buf 中
  FILE *out = open_memstream(&buf, &len);

  va_list va;
  va_start(va, fmt);
  vfprintf(out, fmt, va);
  va_end(va);

  fclose(out);
  return buf;
}
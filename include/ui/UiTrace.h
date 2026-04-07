#pragma once

#include <cstdarg>
#include <cstdio>

inline void ui_trace_log(const char *tag, const char *fmt, ...)
{
  if (!tag) tag = "UI";
  if (!fmt) return;

  std::fprintf(stderr, "[UI][%s] ", tag);

  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);

  std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

#define UI_TRACE(TAG, FMT, ...) ui_trace_log(TAG, FMT, ##__VA_ARGS__)
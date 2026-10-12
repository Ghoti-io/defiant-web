/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * The demo's clock. Preloaded into the defiant program, it adds
 * DEMO_CLOCK_SKEW seconds to time(), so a session's day passes without the
 * demo waiting for it. Linux only, and only for tools/demo.sh.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <time.h>

time_t time(time_t * out) {
  static time_t (*real)(time_t *);
  static long skew = -1;
  time_t now;
  if (real == NULL) {
    real = (time_t (*)(time_t *))dlsym(RTLD_NEXT, "time");
  }
  if (skew < 0) {
    const char * text = getenv("DEMO_CLOCK_SKEW");
    skew = text != NULL ? atol(text) : 0;
  }
  now = real(NULL) + (time_t)skew;
  if (out != NULL) {
    *out = now;
  }
  return now;
}

/* Copyright 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. */

#include "nacl_io/kernel_intercept.h"
#include "nacl_io/kernel_wrap.h"

#if defined(__native_client__) && !defined(__GLIBC__)
int sigismember(const sigset_t* set, int signum) {
  return (*set & (1 << signum)) != 0;
}
#endif

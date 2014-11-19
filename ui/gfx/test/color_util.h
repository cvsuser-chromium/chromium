// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_COLOR_UTIL_H_
#define CC_TEST_COLOR_UTIL_H_

#include <iosfwd>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

namespace gfx {

#define EXPECT_SKCOLOR_EQ(a, b) \
  EXPECT_PRED_FORMAT2(::gfx::AssertSkColorsEqual, a, b)

::testing::AssertionResult AssertSkColorsEqual(const char* lhs_expr,
                                               const char* rhs_expr,
                                               SkColor lhs,
                                               SkColor rhs);

}  // namespace gfx

#endif  // CC_TEST_COLOR_UTIL_H_

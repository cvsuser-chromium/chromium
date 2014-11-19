// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_TEST_TEXTURE_H_
#define CC_TEST_TEST_TEXTURE_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "cc/resources/resource_format.h"
#include "third_party/WebKit/public/platform/WebGraphicsContext3D.h"
#include "ui/gfx/size.h"

namespace cc {

size_t TextureSizeBytes(gfx::Size size, ResourceFormat format);

struct TestTexture : public base::RefCounted<TestTexture> {
  TestTexture();

  void Reallocate(gfx::Size size, ResourceFormat format);

  gfx::Size size;
  ResourceFormat format;
  scoped_ptr<uint8_t[]> data;

  // TODO(mvujovic): Replace this with a hash map of texture parameter names
  // and values, which can hold this filter parameter value and more.
  blink::WGC3Denum filter;

 private:
  friend class base::RefCounted<TestTexture>;
  ~TestTexture();
};

}  // namespace cc

#endif  // CC_TEST_TEST_TEXTURE_H_

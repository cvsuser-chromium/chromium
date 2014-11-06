// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_image_egl.h"

#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_surface_egl.h"

namespace gfx {

GLImageEGL::GLImageEGL(gfx::Size size)
    : egl_image_(EGL_NO_IMAGE_KHR),
      size_(size),
      in_use_(false) {
}

GLImageEGL::~GLImageEGL() {
  Destroy();
}

bool GLImageEGL::Initialize(gfx::GpuMemoryBufferHandle buffer) {
  DCHECK(buffer.native_buffer);
  EGLint attrs[] = {
    EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
    EGL_NONE,
  };
  egl_image_ = eglCreateImageKHR(
      GLSurfaceEGL::GetHardwareDisplay(),
      EGL_NO_CONTEXT,
      EGL_NATIVE_BUFFER_ANDROID,
      buffer.native_buffer,
      attrs);

  if (egl_image_ == EGL_NO_IMAGE_KHR) {
    EGLint error = eglGetError();
    LOG(ERROR) << "Error creating EGLImage: " << error;
    return false;
  }

  return true;
}

void GLImageEGL::Destroy() {
  if (egl_image_ == EGL_NO_IMAGE_KHR)
    return;

  EGLBoolean success = eglDestroyImageKHR(
      GLSurfaceEGL::GetHardwareDisplay(), egl_image_);

  if (success == EGL_FALSE) {
    EGLint error = eglGetError();
    LOG(ERROR) << "Error destroying EGLImage: " << error;
  }

  egl_image_ = EGL_NO_IMAGE_KHR;
}

gfx::Size GLImageEGL::GetSize() {
  return size_;
}

bool GLImageEGL::BindTexImage() {
  if (egl_image_ == EGL_NO_IMAGE_KHR) {
    LOG(ERROR) << "NULL EGLImage in BindTexImage";
    return false;
  }

  // Defer ImageTargetTexture2D if not currently in use.
  if (!in_use_)
    return true;

  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image_);
  DCHECK_EQ(static_cast<GLenum>(GL_NO_ERROR), glGetError());
  return true;
}

void GLImageEGL::ReleaseTexImage() {
  // Nothing to do here as image is released after each use.
}

void GLImageEGL::WillUseTexImage() {
  DCHECK(egl_image_);
  DCHECK(!in_use_);
  in_use_ = true;
  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image_);
  DCHECK_EQ(static_cast<GLenum>(GL_NO_ERROR), glGetError());
}

void GLImageEGL::DidUseTexImage() {
  DCHECK(in_use_);
  in_use_ = false;
  char zero[4] = { 0, };
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               1,
               1,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               &zero);
}

}  // namespace gfx

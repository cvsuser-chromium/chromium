// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ANDROID_TRACING_CONTROLLER_ANDROID_H_
#define CONTENT_BROWSER_ANDROID_TRACING_CONTROLLER_ANDROID_H_

#include "base/android/jni_helper.h"
#include "base/memory/scoped_ptr.h"

namespace content {

// This class implements the native methods of TracingControllerAndroid.java
class TracingControllerAndroid {
 public:
  TracingControllerAndroid(JNIEnv* env, jobject obj);
  void Destroy(JNIEnv* env, jobject obj);

  bool StartTracing(JNIEnv* env,
                    jobject obj,
                    jstring filename,
                    jstring categories,
                    jboolean record_continuously);
  void StopTracing(JNIEnv* env, jobject obj);

 private:
  ~TracingControllerAndroid();
  void OnTracingStopped();

  JavaObjectWeakGlobalRef weak_java_object_;

  class Subscriber;
  scoped_ptr<Subscriber> subscriber_;

  DISALLOW_COPY_AND_ASSIGN(TracingControllerAndroid);
};

// Register this class's native methods through jni.
bool RegisterTracingControllerAndroid(JNIEnv* env);

}  // namespace content

#endif  // CONTENT_BROWSER_ANDROID_TRACING_CONTROLLER_ANDROID_H_

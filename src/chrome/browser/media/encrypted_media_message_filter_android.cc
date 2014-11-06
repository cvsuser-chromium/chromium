// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/encrypted_media_message_filter_android.h"

#include <string>

#include "chrome/common/encrypted_media_messages_android.h"
#include "ipc/ipc_message_macros.h"
#include "media/base/android/media_codec_bridge.h"
#include "media/base/android/media_drm_bridge.h"

using content::BrowserThread;
using media::MediaCodecBridge;
using media::MediaDrmBridge;

namespace chrome {

// Check whether the available codecs are supported.
static android::SupportedCodecs GetSupportedCodecs(
    android::SupportedCodecs requested_codecs,
    bool video_must_be_compositable) {
  android::SupportedCodecs supported_codecs = android::NO_SUPPORTED_CODECS;
  // TODO(qinmin): Remove this DCHECK and query VP8/Vorbis capabilities
  // once webm support is added to Android.
  DCHECK(!(requested_codecs & android::WEBM_VP8_AND_VORBIS));

#if defined(USE_PROPRIETARY_CODECS)
  if ((requested_codecs & android::MP4_AAC) &&
      MediaCodecBridge::CanDecode("mp4a", false)) {
    supported_codecs = static_cast<android::SupportedCodecs>(
        supported_codecs | android::MP4_AAC);
  }

  // TODO(qinmin): Remove the composition logic when secure contents can be
  // composited.
  if ((requested_codecs & android::MP4_AVC1) &&
      MediaCodecBridge::CanDecode("avc1", !video_must_be_compositable)) {
    supported_codecs = static_cast<android::SupportedCodecs>(
        supported_codecs | android::MP4_AVC1);
  }
#endif  // defined(USE_PROPRIETARY_CODECS)

  return supported_codecs;
}

EncryptedMediaMessageFilterAndroid::EncryptedMediaMessageFilterAndroid() {}

EncryptedMediaMessageFilterAndroid::~EncryptedMediaMessageFilterAndroid() {}

bool EncryptedMediaMessageFilterAndroid::OnMessageReceived(
    const IPC::Message& message, bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(
      EncryptedMediaMessageFilterAndroid, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_GetSupportedKeySystems,
                        OnGetSupportedKeySystems)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()
  return handled;
}

void EncryptedMediaMessageFilterAndroid::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
  // Move the IPC handling to FILE thread as it is not very cheap.
  if (message.type() == ChromeViewHostMsg_GetSupportedKeySystems::ID)
    *thread = BrowserThread::FILE;
}

void EncryptedMediaMessageFilterAndroid::OnGetSupportedKeySystems(
    const SupportedKeySystemRequest& request,
    SupportedKeySystemResponse* response) {
  if (!MediaDrmBridge::IsAvailable() || !MediaCodecBridge::IsAvailable())
    return;

  // TODO(qinmin): Convert codecs to container types and check whether they
  // are supported with the key system.
  if (!MediaDrmBridge::IsCryptoSchemeSupported(request.uuid, ""))
    return;

  DCHECK_EQ(request.codecs >> 3, 0) << "unrecognized codec";
  response->uuid = request.uuid;
  // TODO(qinmin): check composition is supported or not.
  response->compositing_codecs =
      GetSupportedCodecs(request.codecs, true);
  response->non_compositing_codecs =
      GetSupportedCodecs(request.codecs, false);
}

}  // namespace chrome

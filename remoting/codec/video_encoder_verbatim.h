// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CODEC_VIDEO_ENCODER_VERBATIM_H_
#define REMOTING_CODEC_VIDEO_ENCODER_VERBATIM_H_

#include "remoting/codec/video_encoder.h"
#include "remoting/proto/video.pb.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_geometry.h"

namespace remoting {

// VideoEncoderVerbatim implements a VideoEncoder that sends image data as a
// sequence of RGB values, without compression.
class VideoEncoderVerbatim : public VideoEncoder {
 public:
  VideoEncoderVerbatim();
  virtual ~VideoEncoderVerbatim();

  // VideoEncoder interface.
  virtual scoped_ptr<VideoPacket> Encode(
      const webrtc::DesktopFrame& frame) OVERRIDE;

 private:
  // Allocates a buffer of the specified |size| inside |packet| and returns the
  // pointer to it.
  uint8* GetOutputBuffer(VideoPacket* packet, size_t size);

  // The most recent screen size. Used to detect screen size changes.
  webrtc::DesktopSize screen_size_;
};

}  // namespace remoting

#endif  // REMOTING_CODEC_VIDEO_ENCODER_VERBATIM_H_
